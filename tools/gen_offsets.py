"""Make src/generated/lab_data.inc from Sky's libBootloader.so (0.35.2): every type the game registers by name.

Only addresses (offsets from the start of libBootloader.so) and 32-bit name checksums are written, never the game's
text: the mod reads names, setting names and designer notes from the running game's memory.

Usage: python3 tools/gen_offsets.py path/to/libBootloader.so [0x29b4e5c]
  0x29b4e5c = the 0.35.2 function that registers a type by name (x0 = type object, x1 = name).
"""
import bisect, re, struct, sys
import numpy as np

LIB = sys.argv[1]
REGISTER = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x29B4E5C
OUT = __import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'src', 'generated', 'lab_data.inc')

d = open(LIB, 'rb').read()
shoff = struct.unpack_from('<Q', d, 0x28)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3A)
secs = {}
raw = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shentsize) for i in range(shnum)]
strtab = raw[shstrndx][4]
for s in raw:
    n = d[strtab + s[0]:d.index(b'\0', strtab + s[0])].decode()
    secs[n] = dict(va=s[3], off=s[4], size=s[5], type=s[1])
text, ro = secs['.text'], secs['.rodata']
w = np.frombuffer(d[text['off']:text['off'] + text['size'] // 4 * 4], dtype='<u4')
pcs = np.arange(len(w), dtype=np.int64) * 4 + text['va']

def cstr(va):
    if not (ro['va'] <= va < ro['va'] + ro['size']):
        return None
    o = va - ro['va'] + ro['off']
    if d[o - 1] != 0:
        return None
    e = d.find(b'\0', o, o + 600)
    if e <= o:
        return None
    try:
        t = d[o:e].decode('utf-8')
    except UnicodeDecodeError:
        return None
    return t if t.isprintable() else None

def fnv1a(s):
    h = 0x811C9DC5
    for b in s.encode('utf-8'):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h

# string references: (pc, string address) for every ADRP followed by ADD on the same register
adrp = np.nonzero((w & 0x9F000000) == 0x90000000)[0]
refs = []
for k in range(1, 4):
    j = adrp + k
    ok = j < len(w)
    a, b = adrp[ok], j[ok]
    wa, wb = w[a].astype(np.int64), w[b].astype(np.int64)
    rd = wa & 31
    addi = ((wb & 0xFF800000) == 0x91000000) & (((wb >> 5) & 31) == rd)
    imm = ((wa >> 29) & 3) | (((wa >> 5) & 0x7FFFF) << 2)
    imm = np.where(imm & (1 << 20), imm - (1 << 21), imm)
    page = (pcs[a] & ~0xFFF) + (imm << 12)
    tgt = page + (((wb >> 10) & 0xFFF) << (12 * ((wb >> 22) & 1)))
    for p, t in zip(pcs[a][addi], tgt[addi]):
        refs.append((int(p), int(t)))
refs = sorted(set(refs))
ref_pcs = [p for p, _ in refs]

# registration calls: BL REGISTER, with x0/x1 tracked back through ADRP/ADD/MOV (reset after calls)
imm26 = (w & 0x03FFFFFF).astype(np.int64)
imm26 = np.where(imm26 & (1 << 25), imm26 - (1 << 26), imm26)
sites = np.nonzero(((w & 0xFC000000) == 0x94000000) & (pcs + imm26 * 4 == REGISTER))[0]
types = []
for i in sites:
    regs = {}
    for k in range(max(0, i - 200), i):
        x = int(w[k]); pc = int(pcs[k])
        if (x & 0xFC000000) == 0x94000000 or (x & 0xFFFFFC1F) == 0xD63F0000:
            for r in range(19):
                regs.pop(r, None)
            continue
        if (x & 0x9F000000) == 0x90000000:
            im = ((x >> 29) & 3) | (((x >> 5) & 0x7FFFF) << 2)
            if im & (1 << 20):
                im -= 1 << 21
            regs[x & 31] = (pc & ~0xFFF) + (im << 12)
        elif (x & 0xFF800000) == 0x91000000:
            rd, rn = x & 31, (x >> 5) & 31
            if rn in regs:
                regs[rd] = regs[rn] + (((x >> 10) & 0xFFF) << (12 * ((x >> 22) & 1)))
            else:
                regs.pop(rd, None)
        elif (x & 0xFFE0FFE0) == 0xAA0003E0:
            rd, rm = x & 31, (x >> 16) & 31
            if rm in regs:
                regs[rd] = regs[rm]
            else:
                regs.pop(rd, None)
    name_va = regs.get(1)
    name = cstr(name_va) if name_va else None
    if name and re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name):
        types.append([int(pcs[i]), name, name_va, regs.get(0, 0)])

all_sites = sorted(t[0] for t in types)

# ---- setting descriptors: the game builds one record per setting in static initialisers:
# {+0x00 name, +0x08 attribute list, +0x20 offset in the event, +0x28 type handler}; attribute nodes are
# {+0x00 key (none = default value), +0x08 value, +0x10 next}. Collected by following stores into those records.
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
def descriptors(pc, end):
    regs, mem = {}, {}
    o = pc - text['va'] + text['off']
    for i in md.disasm(d[o:o + (end - pc)], pc):
        m = i.mnemonic
        ops = [t.strip() for t in i.op_str.replace('[', ' ').replace(']', ' ').replace('!', '').split(',')]
        try:
            if m in ('adrp', 'adr'):
                regs[ops[0]] = int(ops[1].lstrip('#'), 16)
            elif m == 'add' and len(ops) == 3 and ops[2].startswith('#'):
                regs[ops[0]] = regs[ops[1]] + int(ops[2].lstrip('#'), 16) if regs.get(ops[1]) else None
            elif m in ('mov', 'movz') and len(ops) == 2 and ops[1].startswith('#'):
                regs['x' + ops[0][1:]] = int(ops[1].lstrip('#'), 16)
            elif m == 'mov' and len(ops) == 2:
                regs[ops[0]] = 0 if ops[1] in ('xzr', 'wzr') else regs.get(ops[1])
            elif m in ('stp', 'str'):
                disp_given = ops[-1].startswith('#')
                base = ops[-2] if disp_given else ops[-1]
                disp = int(ops[-1].lstrip('#'), 16) if disp_given else 0
                if regs.get(base):
                    srcs = ops[:2] if m == 'stp' else ops[:1]
                    size = 8 if srcs[0].startswith('x') else 4
                    for n, r in enumerate(srcs):
                        v = 0 if r in ('xzr', 'wzr') else regs.get('x' + r[1:] if r.startswith('w') else r)
                        mem.setdefault(regs[base], {})[disp + n * size] = v
            elif m == 'bl':
                for r in range(19):
                    regs.pop('x%d' % r, None)
            elif m == 'ret':
                break
        except (KeyError, ValueError, TypeError):
            pass
    out = []
    for rec in mem.values():
        nva, h = rec.get(0), rec.get(0x28)
        nm = cstr(nva) if nva else None
        if not nm or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', nm) or not isinstance(h, int) \
                or not (text['va'] <= h < text['va'] + text['size']):
            continue
        attrs, node, n = [], rec.get(8), 0
        while node and node in mem and n < 12:
            attrs.append((mem[node].get(0), mem[node].get(8)))
            node, n = mem[node].get(0x10), n + 1
        out.append(dict(name=nm, name_va=nva, offset=rec.get(0x20) or 0, handler=h, attrs=attrs))
    return sorted(out, key=lambda f: f['offset'])

ENUM_CONST = re.compile(r'^(k[A-Z0-9][A-Za-z0-9]*_|[A-Z][A-Za-z0-9]*_)[A-Za-z0-9_]+$')
type_settings = {}
for pc, name, name_va, obj in types:
    k = bisect.bisect_right(all_sites, pc)
    end = min(all_sites[k] if k < len(all_sites) else pc + 0x1000, pc + 0x1400)
    type_settings[name] = descriptors(pc, end)
# enum options: constants registered with an enum's handler (anywhere), in registration order
enum_opts = {}
for fs in type_settings.values():
    for f in fs:
        if ENUM_CONST.match(f['name']):
            enum_opts.setdefault(f['handler'], [])
            if f['name_va'] not in [v for v, _ in enum_opts[f['handler']]]:
                enum_opts[f['handler']].append((f['name_va'], f['name']))
# setting kinds by type handler (Sky 0.35.2); enums and objects are recognised from their data
KIND = {0xDD7F30: 1, 0xDF5F9C: 2, 0xE7C718: 3, 0xF6CC58: 3, 0xDDB4A0: 4, 0xDDFE0C: 5, 0xFA4354: 6, 0x135B874: 7}
# 0 unknown, 1 bool, 2 float, 3 int, 4 text, 5 vec3, 6 colour rgb, 7 vec4/rgba, 8 enum, 9 object reference
def kind_of(f):
    if f['handler'] in KIND:
        return KIND[f['handler']]
    if len(enum_opts.get(f['handler'], [])) >= 2:
        return 8
    keys = [cstr(k) if k else None for k, _ in f['attrs']]
    if 'Tool_ObjectType' in keys or 'Tool_Nullable' in keys:
        return 9
    return 0


def category(n):
    if re.match(r'(Account|Server|Analytics|IAP|Commerce|Login|Auth|StarWatch)', n): return 13
    if re.search(r'(Rpc|RPC|Request|RequestEvent|Response|Resource|Paginated|Client|Service)$', n): return 13
    if re.search(r'(Purchase|Redeem|Currency|Unlock|Inventory|Consum|Lootbox|Gift|Reward|Shop|Buy|Spend|Trust|Ack[A-Z])', n): return 13
    if n.startswith('BT_'): return 11
    if re.search(r'(Job|Barn|Defs?|Manager|Pool|Engine|Cache|Loader|Socket)$', n): return 12
    for c, p in enumerate([
        r'^(Avatar|Player|Local|Flight|Fly|Swim|Dive|Glide|Run|Walk|Jump|Stance|Emote|Shout|Sing|Hug|Hand)',
        r'(Camera|Photo|Cinematic|Lens|Zoom|Fov|Dof|Screenshot|Selfie|Bullet)',
        r'(Ambience|Weather|Rain|Wind|Sun|Sky|Cloud|Fog|Light|Glow|Color|Desaturat|Bloom|Effect|Fx|FX|Particle|Firework|Star|Aurora|Rainbow|Snow|Water|Ocean|Shader|Render|Material|Mesh|Wing)',
        r'(Music|Sound|Audio|Song|Instrument|Arpegg|Piano|Note|Melody|Haptic)',
        r'(Menu|Button|UI|Ui|Hud|HUD|Banner|Text|Title|Card|Dialog|Popup|Hint|Prompt|Page|Screen|Tutorial)',
        r'(Social|Friend|Remote|MP$|Shared|Audience|Bench|Chat|Message|Party|Invite|Relationship|Spotlight|Group)',
        r'(Level|Warp|Teleport|Portal|Gate|Door|Map|Realm|Home|Spawn|Checkpoint|Travel|Nav)',
        r'(Npc|Spirit|Ancestor|Elder|Krill|Dark|Crab|Manta|Jelly|Bird|Creature|Whale|Butterfly|Mote|Shard)',
        r'(Quest|Stat|Achievement|Season|Event|Challenge|Mission|Candle|Wax|Collect|Eden|Storm|Daily|Calendar|Anniversary|Anni|Days)',
        r'(Prop|Placeable|Toy|Ball|Game|Race|Kite|Swing|Seesaw|Trampoline|Balloon|Bonfire|Tea|Table|Chair|Tub|Puzzle|Sack|Boost|Ramp|Figurine|Plush|Staff|Shield|Spear)']):
        if re.search(p, n):
            return c
    return 10

settings_out, notes_out, enums_out, enum_index = [], [], [], {}
rows = []
for pc, name, name_va, obj in sorted(types, key=lambda t: t[1].lower()):
    k = bisect.bisect_right(all_sites, pc)
    end = min(all_sites[k] if k < len(all_sites) else pc + 0x800, pc + 0x1000)
    for a in range(pc, end, 4):
        if d[a - text['va'] + text['off']:a - text['va'] + text['off'] + 4] == bytes.fromhex('c0035fd6'):
            end = a
            break
    s0, n0 = len(settings_out), len(notes_out)
    descs = set()
    for f in type_settings.get(name, []):
        if ENUM_CONST.match(f['name']) or len(settings_out) - s0 >= 64:
            continue
        kd = kind_of(f)
        dflt = desc = objt = 0
        for kva, vva in f['attrs']:
            key, val = (cstr(kva) if kva else None), (cstr(vva) if vva else None)
            if val is None:
                continue
            if key is None and not dflt:
                dflt = vva
            elif key == 'Tool_Description':
                desc = vva
                descs.add(vva)
            elif key == 'Tool_ObjectType':
                objt = vva
        en = 0xFFFF
        if kd == 8:
            if f['handler'] not in enum_index:
                enum_index[f['handler']] = len(enums_out)
                enums_out.append(enum_opts[f['handler']])
            en = enum_index[f['handler']]
        settings_out.append((f['name_va'], dflt, desc, objt, min(f['offset'], 0xFFFF), kd, en))
    seen = set()
    for _, va in refs[bisect.bisect_right(ref_pcs, pc):bisect.bisect_left(ref_pcs, end)]:
        s_ = cstr(va)
        if s_ and va not in seen and va not in descs and s_.startswith('"') and s_.endswith('"') \
                and len(notes_out) - n0 < 8:
            seen.add(va)
            notes_out.append(va)
    rows.append((name_va, fnv1a(name), pc, obj, category(name), s0, len(settings_out) - s0, n0, len(notes_out) - n0))

opt_flat = [va for opts in enums_out for va, _ in opts]
with open(OUT, 'w') as f:
    f.write('// Generated by tools/gen_offsets.py from libBootloader.so (Sky 0.35.2). Addresses only: offsets from the\n')
    f.write('// start of libBootloader.so. Text (names, settings, notes, options) is read from the running game.\n')
    f.write(f'#define LAB_TYPE_COUNT {len(rows)}\n#define LAB_REGISTER_FN 0x{REGISTER:X}\n')
    f.write('static const LabType kTypes[] = {\n')
    for r in rows:
        f.write('{0x%X,0x%08X,0x%X,0x%X,%d,%d,%d,%d,%d},\n' % r)
    f.write('};\n// name, default value, description, object type (string addresses), offset in the event, kind, enum\n')
    f.write('static const LabSetting kSettings[] = {\n')
    for r in settings_out:
        f.write('{0x%X,0x%X,0x%X,0x%X,0x%X,%d,%d},\n' % r)
    f.write('{0,0,0,0,0,0,0}};\nstatic const LabEnum kEnums[] = {\n')
    first = 0
    for opts in enums_out:
        f.write('{%d,%d},' % (first, len(opts)))
        first += len(opts)
    f.write('{0,0}};\nstatic const uint32_t kEnumOptions[] = {\n')
    for i in range(0, len(opt_flat), 8):
        f.write(','.join('0x%X' % v for v in opt_flat[i:i + 8]) + ',\n')
    f.write('0};\nstatic const uint32_t kNotes[] = {\n')
    for i in range(0, len(notes_out), 8):
        f.write(','.join('0x%X' % v for v in notes_out[i:i + 8]) + ',\n')
    f.write('0};\n')
import collections
print(len(rows), 'types,', len(settings_out), 'settings', dict(collections.Counter(r[5] for r in settings_out)),
      len(enums_out), 'option lists,', len(notes_out), 'notes ->', OUT)

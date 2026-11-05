# Offset Lab

An experimental [Canvas / Sky Modloader](https://github.com/books4jy-art) mod for exploring the game library of
**Sky: Children of the Light 0.35.2** (Android, arm64), in the **Sky Mesh Lab** theme
([sky-mesh-explorer-labs](https://github.com/books4jy-art/sky-mesh-explorer-labs)): near-black, white text, thin
borders, sky blue accents.

It exists to satisfy curiosity: try things and see what they do. Expect crashes; use it on a private server.

## Install

Download `libOffsetLab.so` from the [latest release](../../releases/latest) and import it into Canvas as a mod.

## Window

Offset Lab draws its own window (`selfManagedUI`), laid out like TSM's: a full-height panel docked on one side (500 dp
wide, 360–720 with **Width**), opened and collapsed with the **Lab** handle. Like TSM's slide handle, it sits on the
panel's inner edge (on the screen edge when collapsed); drag it up or down to move it, or across the screen to dock
the panel on the other side. **Display** (top of the panel) has Size, Width and Scrollbar (default 40 px, 20–80). Sizes are TSM's, in dp
(1.6667 px × TSM's auto-scale, which fits its 430×545 dp layout into 75% × 80% of the screen, at most 1): 12 dp
margins, rows at least 36 dp high, 44 dp sub-tab buttons, text 15 dp high. **Size** scales it all.
The Features tab's sub-tab buttons fold into one 44 dp header line showing the current sub-tab and an arrow: tap it to open or close the button grid; picking a sub-tab folds it again. Whether it is folded is saved with the other settings (`featuresTabsCollapsed` in `offsetlab_settings.txt`).
Lists scroll by dragging the content (a vertical drag that starts on a button scrolls instead of pressing it).
Features that repeat keep running while the panel is collapsed.

**Fast presses.** Each press queues a script for the game's main thread. A newer press for the same feature replaces
one still waiting (only the latest counts), at most 4 scripts run per frame, and the same feature runs at most every
0.15 s: pressing quickly used to start, switch off and restart events many times in one frame and could crash the game.

**Memory.** The game's outfit and collectible files are read once and only `name`, `type`, `icon` and `max_level` are
kept (the rest is read past); the lists are re-filtered only when the category or search changes.

**Handles.** Events that do their thing and finish (spells, outfits, shout, emotes, bounce, flash, shake, kill…) are
deleted by the game by themselves, so they're never kept or switched off: switching off a deleted event crashed the
game ("Invalid meta class id"). Kept events are dropped when the level changes, for the same reason.

## Tabs

| Tab | What it does |
|---|---|
| **Features** | Ready-made controls, laid out like TheVoid's menu (see below). |
| **Spawn** | Creatures and effects next to you, local to your game: the game's own developer spawner for birds, mantas, ghost mantas, butterflies, jellyfish, sky kids, serow, crabs and fish (count and distance sliders, one school at a time), shard fall, star shower and darkstone rain, **Remove** for each and **Remove all spawned**, the developers' fish school debug switch, and the kinds that can't be offered with the reason. See *Spawn* below. |
| **Recorder** | Records every event the game starts while you play (its own and the mod's), with the time and the values it used. **Start / Stop / Clear**, a filter, **Hide noisy** (types started more than 4 times a second, e.g. every frame), and per row: time since Start, readable title, ×count when repeated, **Replay** (starts it again like the features do: the game's defaults, then the recorded values; only events, server types only with *Server types* on) and **Open in Game functions**. **Save** writes `offsetlab_recording.txt` next to the mod (time, name, values; overwritten each time). See *Recorder* below. |
| **Game functions** | Every type the game registers by name (2,751). Only **events** (types that descend from the game's Event type) can be fired; asking the game to fire anything else makes it abort, so Fire is off for them and *Events only* is on by default. Each is shown with a readable title ("Avatar set inner glow") and the game's name under it. **What it does**: the game's own notes for it and the developers' description of each setting, where they wrote one. **Settings**: each gets the control that fits its type in the game (toggle, slider with typing, whole number, text, x/y/z, colour picker, a choice of the game's own options, or a Lua object), starting at the game's default; only changed settings are sent. Under each setting, **Accepts** says what it takes: the game's own valid range where it gives one (`Tool_MinMaxValues`, 87 settings; sliders and typing are held to it), otherwise its type and a suggested range. Object settings (markers, NPCs, Float nodes…) refuse numbers and text, since the game would use them as objects and crash. Where the game keeps no type for a setting, it's guessed from the name and labelled so. **Fire**, **Fire on change**, **Stop**, **Back / Forward / Recent**. Offsets are under *Offsets*. |
| **Lua** | Run any Lua in the game. The result is the game's code (0 = no error). |
| **Memory** | Read-only view of any offset in libBootloader.so (or an absolute address): hex, text and the value as integer, float, double or pointer, with *follow pointer*. Reads never crash: unreadable memory shows `??`. |
| **Native** | The functions the game exports by name, read from its own symbol table. **Watch** counts calls per second and shows the last four arguments; *Watch any offset* takes a hex offset (it must be the start of a function). |
| **Other offsets** | TheVoid 0.1.2's feature table (79 addresses: its names and Sky 0.35.1 addresses), ported to 0.35.2 by matching the game code that uses each one, with how each was ported and, where known, what it likely is. Kept for exploring: open any of them in Memory, or watch the code ones. |
| **Log** | What the mod did. |

### Features

Lua runs on the game's main thread: scripts are queued and run at the start of a function the game calls every frame
(`0x17BB458`; TheVoid's Lua update hook, `0x17BB310` in 0.35.1, hooked with Dobby after checking its first
instruction). Running Lua from the window's own thread raced the game's Lua and could crash it. Every event a feature
starts gets all its settings at the game's defaults first (objects such as `npc` set to nil): events made from Lua
don't get them, and AvatarDisableShadow crashed reading an uninitialised `npc`.

Each one starts one of the game's own events from Lua (kept as `OffsetLab[key]` in the game's Lua, so it can be
switched off with `SetEnabled(false)`: events have only `Start`, `SetEnabled` and `GetEnabled` in the game's Lua, no
`Stop`) or sets one of the game's variables. Toggles run while on; sliders apply when you let go, and every slider has
**Reset**, which puts the game's normal behaviour back. All local to your game.

The tab has sub-tabs: Player, Energy, Transform, Outfits, Items, Emotes, Camera, World, Teleport, Social, Shout (the Social sub-tab: speak to all, read all messages, take out candle, show others, force light / unlight all, offer emotes, friendship tree).

| Feature | Control | Uses (0.35.2) |
|---|---|---|
| Player hover | slider (height, 0–20) | `AvatarAntiGravity`: rise at 3/s for height/3 s, then upward speed 0 and gravity 0 to hold there; Reset switches it off and you land |
| Antigravity | toggle | `SetAvatarGravityMult` 0 / 1 |
| Speed up / down | slider (0.1–6×) | `SetAvatarRunSpeedMult` |
| Auto walk | toggle, speed and direction sliders | `SetAvatarVelocity` (x/z held while on) |
| Pogo stick | power slider, Bounce, keep bouncing + interval | `SetAvatarMinYSpeed` (take off) |
| Crawl mode | toggle | `AvatarCrawl` 1; off: `AvatarCrawl` 0 and `AvatarSetStance` stand (stopping the event alone leaves you crawling) |
| Scatter, Force flight, Walk under water, Kill avatar (again every 3 s) | toggles | `AvatarBreakHandHold` (every 0.5 s), `AvatarForceEnableFlightMode`, `WalkUnderWater`, `KillLocalAvatar` |
| Stop all animations | toggle | `AvatarPauseAnim` for 1 s, renewed every 0.5 s while on |
| T-pose | toggle; any animation name with Play / Stop | `PlayAvatarAnimSeq` (`AirplaneArmsEmote_1`), then `AvatarPauseAnim`; `StopAvatarAnimSeq` |
| Neck turn (experimental) | toggle (the head follows the camera) | `AvatarLookAt` at a marker kept at the camera by `GetCameraTransform`. No sliders: reading a marker's position from Lua (`marker:pos()`) crashes the game |
| Land now | button | `AvatarExitFlight` with `fireOnce` (update `0x124BE88` stops itself) |
| Dash / launch | speed slider; Launch up, Dash +X, Dash −X | `SetAvatarVelocity`, one push (`fireOnce`). World axes: the event writes its Z value into the up slot (`0x123B538`), so no forward/Z push |
| Glide & lift | glide angle and lift toggles + sliders, Reset | `SetFlightParams` (`cruiseAngleDegrees`, `liftMult`) held while on |
| Ignore wind | toggle | `SetAvatarWindParams` (`noStandingWind`, `noStandingBoom`, `noWindInHitRecover`, boom threshold 100000) held while on |
| No jump, No cliff jump, No weak jump down, No bounce | toggles | `DisableJump`, `DisableCliffJump`, `DisableWeakJumpDown`, `DisableLocalPlayerBounce` while on |
| Fast flap | toggle | the flap's busy time (`fmadd` at `0x22D49F0`, 1–2 s) becomes `fmov s0, #0.5`; off puts the word back |
| Scooter mode | disabled | `0x12343FC` (TSM's label) zeroes your velocity on the ground when a byte is set: not a scooter |
| Freeze in place | toggle | `AvatarSetPos` back to the spot you were at (read natively), every 0.2 s; lets go on a level change |
| Stances | Sit, Squat, Sleep, Idle sit, Frozen, Default | `AvatarSetStance` (`avatarState`, `overwriteState`, `instant`, once) |
| Weak walk, Nervous walk | toggles | `AvatarPoseForceWalkCycle` `forceWeak` / `forceNervous` while on |
| Smooth shrink / grow | speed, size and height sliders (−3..3), Reset | `AvatarShrinkGrow` (−3, its default, is the smallest); Reset grows back to your own values (`0x3139710`/`0x313970C`), then stops |
| Ghost look, Fade all avatars | toggles | `SetAvatarGhost` (+ `disableOmniLight`); the fade switch byte `0x312F490` (read at `0x11A8E08`) |
| Charcoal look | disabled | `0x312F4B4` is the "players can be unlit" switch, not a look |
| Tattered cape | toggle | `SetAvatarCapeTattered` amount 1; off: amount 0, then stopped |
| Inner glow | slider, Reset (0) | `AvatarSetInnerGlow` (once, stays) |
| Hide flight trail | toggle | `HideAvatarFlightTrail` while on |
| Anti-AFK | toggle | the AFK check (`0xF8F330`) ends in `cset w0, gt` at `0xF8F374`, patched to `mov w0, wzr`; your device only |
| Fast home | toggle | the home hold's 5.25 s wait (`b.lt` at `0x1EBE348`) a NOP: home after 0.25 s |
| Follow | *Look for access* (writes the game's Lua API to `offsetlab_lua_api.txt`) | not available yet: no event follows another player, and their positions aren't read yet |
| Transform avatar | scrolling buttons (forms; *Other spells too*), duration slider, any spell id, *Back to normal* | `GrantSpell` (not networked), `RemoveSpell` |
| Winged light, temporary | toggle + slider + typing | `AvatarOverrideCapeLevel` (reverts when stopped) |
| Winged light, permanent (experimental) | Collect / Deposit / Convert / Drop, behind a server switch that's off at every start | the game's own `…WingBuff…ServerRequestEvent`s; the server decides |
| Change outfits | categories with the game's icons, scrolling list with icons and names, search, *NPC outfits too*, *Back to my outfit* | the game's `Data/Resources/OutfitDefs.json` (read through Canvas), `AvatarSetOutfit` |
| Edit outfits | height and size sliders; a button per slot (body, cape, hair, hat, mask, face, neck, horn, feet, prop) that opens the game's outfit editor to move, rotate and resize that piece | TheVoid's avatar height/scale values (`0x313970C`, `0x3139710`; `0x31396FC`/`0x3139700` in 0.35.1); the game's Lua functions `DebugBodyDef(game)`, `DebugWingDef(game)`… (the editor TSM's Outfit Debug Menu opens) |
| Emotes | level, speed, loop and mirror; scrolling list with icons; Stop | `Data/Resources/CollectibleDefs.json`, `AvatarPlayEmote` |
| Camera | shake (speed, force, time); colour filters with strength and invert; random tint, surreal blue, caustics, sketch, hide cinematic bars; rain on screen (amount, size, density); slow motion; photo flash; lens presets; manual camera (experimental) | `SetCameraShake`, `CameraDesaturationEffect`, `CameraRandomTint`, `CameraSurrealBlueEffect`, `CameraCausticsEffect`, `CameraArtBookEffects`, `DisableCinematicBar`, `CameraScreenRainDropEffect`, `SetBulletTime`, `CameraPhotoFlash`, `LevelCameraDefaults`, `ManualControlCamera` |
| No collision (experimental) | toggle | the call that fills the collision list (`0x296CAD0`, `bl 0x296FCC0`) replaced by a NOP; its count is zeroed just before, so nothing is found to collide with. Off restores the call |
| Hide terrain | toggle | the game's terrain switch, byte at `0x3169CEC` (TheVoid's `world.hide_terrain`, `0x3169CDC` in 0.35.1): hides the visible ground only |
| Disable reset barrier | toggle (experimental) | `AvatarSetBoundaryPlane` with a marker at y = -100000 (with no marker the game asserts "No marker provided for boundary plane") |
| Disable shadow | toggle | `AvatarDisableShadow` |
| Surface friction | slider | the game's "slippery ground" switch (byte at `0x34AFCEC`) and the value it loads (instructions at `0x249E524`, normally 0.2), patched to the slider's value; Reset restores both |
| Shout | colour, intensity, voice type, Shout button, repeat + rate | `AvatarDoShout`, `AvatarSetVoice` (temporary) |
| Charge energy (Energy) | amount slider + Charge | `AvatarChargeEnergy` (`addAmount`, the game's energy units; a one-shot) |
| Infinite energy (Energy) | toggle | the game's auto-charge switch (byte `0x3131634`, always charging as if at a light) and no-rain-drain switch (byte `0x3131648`); off: both 0 again |
| Infinite oxygen (Energy) | toggle | `AvatarSetEnergy` (set breath, continuous: breath kept full every frame); off: switched off |
| Invincibility (Energy) | toggle | the game's invincibility switch (byte `0x3131638`: energy and breath only go up, breath kept full); off: 0 again |
| Revive avatar, Break / Restore elder mask (Energy, experimental) | buttons | the game's debug commands `ReviveAvatar(game)`, `BreakElderArmor(game)`, `RepairElderArmor(game)` (checked to exist first) |
| Mini mode (Transform, experimental) | toggle | `SetAvatarMicroMode` (its update shrinks every avatar each frame while it runs, on your screen); off: switched off |
| Broom mode (Transform, experimental) | toggle | `AudienceSettings` with *replace local avatar* and the `Broomstick` look; off: the neutral settings for a moment, then switched off |
| Swim on ground / through air (Transform) | disabled | not possible yet: nothing switches the avatar to swimming outside the game's water volumes, and its water check isn't verified for 0.35.2 |
| Glowstick mode (Items, experimental) | 9 colours + Rainbow, Clear | `AvatarRainbowParty` (a fixed colour or the shifting hue, your avatar only), one at a time |
| Glow (Items) | buttons, one at a time, Clear | `GrantSpell` (`glow`, `glow_rainbow`, `glow_tgc`, `hairtint`, `hairtint_rainbow`; until cleared), the previous one removed with `RemoveSpell` in the same script |
| Walk effects (Items) | buttons, one at a time, Clear | `GrantSpell` / `RemoveSpell` with the game's ground, foot, bloom and flight trail spells |
| Dye outfits (Items, experimental) | slot (or all slots), primary / secondary, a button per dye colour, Random, Remove | `AvatarSetOutfitDye` (local and temporary; the colour names are the game's own, read from `PlacePlaceable`'s dye options) |
| Dye menu (Items) | button | the game's `OpenCraftingMenu(game)` (the dye station menu, anywhere). Not a toggle: nothing closes it from outside; dyeing there is real |
| Spawn closet (Items, experimental) | Spawn / Remove | `OutfitSelect` as a closet with no region, its button marker put at the camera by `GetCameraTransform` once and dropped to the floor |
| Bucket brigade (Items) | disabled | not possible yet: the bucket is a prop event (`Bucket`, an `AvatarPropEvent`) that needs an equipped bucket prop |
| Speak to all (Social, experimental) | text + Send | `AvatarSpeak` (`message`; a one-shot) |
| Read all messages (Social) | toggle | the chat bubble's "may show text" value: `mov w22, wzr` at `0x1478CB0` and `orr w22, w0, w8` at `0x1478CE4` become `mov w22, #1` (each word checked first); off: both put back |
| Take out candle (Social, experimental) | Red candle, White candle, Put away | Red: `AvatarSetCandleState` with `state` = `kAvatarCandleFlameState_On` (the game takes out your own player candle; a one-shot), after an Off so a white one is replaced. White: `SocialGift` with no friend chosen (the game puts the candle away and takes out the premium, white candle; nothing is sent or spent). Put away: `AvatarSetCandleState` with `kAvatarCandleFlameState_Off` |
| Show others client side (Social) | toggle | the ghost fade of every avatar (`0x120F73C`) clamps its amount with `fmov s2, #1.0` at `0x120FA00`; it becomes `movi d2, #0`, so hidden, faded and unlit-looking players are drawn normally (your screen only). Off: the word put back |
| Force light all (Social) | toggle | your screen: the unlit check reads the game's unlit-players switch (`ldrb w12` at `0x11A048C`, switch byte `0x312F4B4`); it becomes `mov w12, wzr`, so nobody is marked unlit (avatar state bit 0, halfword at avatar `+0x10A0C`). Off: the word put back |
| … also light them for real (Social, server switch) | checkbox under Force light all | others see it: the game's own light-a-player function (`0x11A84D4`: avatar, Game `+0x6B0`), the one the candle calls when two flames touch (`0x18703BC`) and `OrbitHandholdingScene` calls for every unlit avatar (`0x11FC460`). Called on the game's main thread (frame hook) for one player every 0.5 s: other players in the level that aren't your own avatar, are in the level (bit 3), weren't lit before (bit 2, bit 14) and haven't been sent a light (`+0x10A78`; the game sends each light once). Off: stops; lights already given stay, as in the game |
| Unlight all (Social) | toggle | your screen only (the game has no way to take light away for others): `cbz w12` at `0x11A0498` becomes a NOP and `eor w8, w10, #1` at `0x11A04AC` becomes `mov w8, #1`, so every other player in the level is marked unlit; your own avatar stays out (the `csel` at `0x11A04B4`). Switches Show others and Force light off. Off: both words put back |
| Offer emotes to strangers (Social) | toggle | the player menu's builder (`0x1D97C88`) asks "is this player my friend in this session" (`bl 0x17A19E0` at `0x1D97D6C`; not a friend: trust tier 0/1 abilities are left out, `0x1D98C64`), patched to `mov w0, #1`; and the node builder's "base ability bought with this friend" check (`tbz` at `0x1D94810`, strangers have no friend record) a NOP. Use with Unlock friendship tree. The offer is the game's own (`0x118DE84`: your avatar, the ability, the target's avatar id; no friend record needed); the other player's game and the server decide whether it can be accepted. Off: both words put back |
| Unlock friendship tree (Social, client side) | toggle | the friendship level (`0x24A92F8`, Game `+0x830` → `+0x10`): `ldr w0, [x0, #4]` at `0x24A9390` becomes `mov w0, #levels` and `cbz x0` at `0x24A938C` a NOP, `levels` being the size of the game's own tier list (read each 2 s; more aborts the game at `0x24AF7FC`). The menu then shows every ability unlocked (`trust_error_tier_locked` check, `0x1D775A0`). The server and the other player still decide. Off: both words put back |
| Audience mode (Social, experimental) | toggle | `AudienceSettings` (reaction menu, no reservation); off: the neutral settings for a moment, then switched off |
| Unlock all (Social, local only) | toggle | the game's "has unlock" check (`0xDFC144`): its branch to the second lookup (`cbz x0` at `0xDFC174`) becomes a NOP so it always returns yes. The server still decides anything it checks. Off: the instruction put back |

Not yet: shout pitch (0.35.2 has no setting for it), first-person view and unlimited zoom
(camera code changes not checked for 0.35.2 yet), Follow.

Every toggle puts the game back when switched off: kept events are switched off, switches and code words get
their 0.35.2 values back. Events that stop by themselves (their update calls the game's event stop, `0x171F82C`:
`AvatarSetOutfitDye`, `AvatarSpeak`, `AvatarChargeEnergy`, spells…) are never kept. List settings (`State_1`…`State_8`,
e.g. OutfitSelect's `onClosetOpen`) get nil, like objects.

Icons and the outfit/emote lists come from Canvas's own API (`IconLoader::getUIIcon`, `Cipher::read_asset`,
declared in `src/canvas_api.h`); nothing from the game is stored in this repository.

### Teleport (Features sub-tab)

Shows the current level and your position at the top. The avatar is always moved by the game's own `AvatarSetPos`
event with one marker (`game:markerBarn():CreateMarker(0)`, set with `m:pos({x, y, z})`) placed at the target; the
position is only ever **read**, never written. All Lua goes through the main-thread queue (keys `teleport`,
`changelevel`, `levellist`). Coordinates outside ±100000 or not finite are refused.

| Part | What it does | Uses (0.35.2) |
|---|---|---|
| **Teleport To Realm** | search, scrolling list grouped by realm (Home & Aviary, Isle of Dawn, Daylight Prairie, Hidden Forest, Valley of Triumph, Golden Wasteland, Vault of Knowledge, Eye of Eden, Events & other), names made from the level ids ("Prairie_ButterflyFields" → "Prairie: Butterfly Fields"; the id in the tooltip), *Reload this level* | the game's `ChangeLevel` event (`levelName`, `isLevelComplete` false). List: 72 known level ids (the levels other mods teleport to: TSM's level list, as level ids are facts; TheVoid's own level names are encrypted in its library), 44 more level-like ids from 0.35.2's strings behind *Unconfirmed ids too* (marked `?`; they may not be levels), plus the game's own lists when available: `Data/Scripts/LevelSelect.lua` / `Data/Raw/Scripts/LevelSelect.lua` read through Canvas (its `level("Id", …)` entries; the files TheVoid reads), and **Ask the game**, which has the game's Lua write its `ArcLevels` table to `offsetlab_levels.txt` next to the mod |
| **POI Menu** | the level links (doorways between levels) of the current level, with where they lead and how far they are: the button moves you onto one (height above it: slider), **Enter** changes to the level it leads to | read natively every second while shown: Game `+0x528` → LevelLinkBarn; its count (u32) at `+0x4E70`, links inline from `+0x70`, `0xD0` bytes each (max 96); in a link: transform (4×4) at `+0x30`, `linkName` `+0x80`, `levelName` `+0x98` (libc++ `std::string`), `enabled` `+0xCC`. Links whose transform isn't affine or whose position isn't sane are skipped |
| **Coordinates** | x/y/z typed (or dragged), *Use my position*, **Teleport**, **Back** (to where you were before the last teleport), *Snap to the ground* (`onGround`) | `AvatarSetPos` (`marker`; `relMarker` nil, `bstGuid` 0, `randomGroundInMarker`/`setLevelStartPosition` false, `breakHandHold`/`killMomentum` true) |
| **Quick Movement** | distance slider (0.5–100 m), Up / Down / Forward / Back / Left / Right, *Hold to repeat* | current position (below) + `AvatarSetPos`. Presses within 0.6 s add up from the last target. *Relative to facing* (experimental) uses the float after the position (TSM's avatar rotation, `+0x20`: forward = (sin, cos)); off, or when that value doesn't look like an angle: world axes (forward = +z, right = +x) |

**Avatar position chain** (each hop checked in the 0.35.2 code): game object (`0x33A5E80`) `+0x318` → AvatarBarn
(the Game reflection record "avatarBarn", built at `0x17C52D8`); the local avatar is `barn + 0x30` when the byte at
`barn + 0xB8A0` is set and bit 3 of the halfword at `barn + 0x10A3C` is set (exactly what the game's
`GetLocalAvatar(barn, true)` at `0x11B2BC4` checks); avatar `+0x18` → `{x, y, z}` (the game prints it as "pos" with
"%.2f,%.2f,%.2f" at `0xFB3934`; 151 callers of `0x11B2BC4` read a vector there). Values must be finite and < 100000.
Current level: the Game record "levelName" (`+0x960`), shown only if it reads as a level id.

Experimental: Unconfirmed ids (changing to an id that isn't a level may hang or crash the game), *Relative to facing*
(the yaw offset is TSM's, not checked in the code), POI positions (the translation is taken from the link's stored
transform). Not done: event portals (`Portal` objects in the PortalBarn, Game `+0x660`; its storage isn't worked out
yet), TheVoid's `Objects.level.bin` points (shrines, waypoints: binary format not known here).

### Spawn

Everything the tab makes is local and is deleted by the game when the level changes (the mod forgets it then too).
**Remove all spawned** clears the creatures and switches off the shard fall and the rain.

| Part | Control | Uses (0.35.2) |
|---|---|---|
| **Creatures** | count (1–60, the game's 10), distance in front of the camera (2–31 m, the game's 5), a button per kind with the game's icon, *Remove creatures*, the kind there is now | the fish school barn (Game `+0x4C8` → `+0x10`) and its **test school** (`+0x19B58`): `CreateTestSchool(barn, FishSchoolType, ball)` at `0x173C14C`, called on the game's main thread (frame hook) exactly as the developers' *Fish Schools* window does for its *Create Test …* buttons (`0x173F940`…`0x1740C80`): Birds 2, Mantas 3, Ghost mantas 8, Butterflies 4, Jellyfish 5, Sky kids flying 6 / abstract 7, Serow 9, Crabs 14, Fish ball 100 (ball, 200 fish), Angel 105, Angler 106, Barracuda 107, Butterfly fish 108, Mola mola 109, Original 110, Sail 111, Tuna 112. It releases the school there was, makes the new one 5 m in front of the camera (the barn's camera, `+0x50` → `+0x10`, checked first) and the barn moves it on with you. Count and distance: the spawner's `mov w8, #0xa` (`0x173C1B8`) and `fmov v1.4s, #5.0` (`0x173C364`), each checked, set for that one call and put straight back. Remove: `ReleaseSchool(barn, school)` (`0x17492FC`) and `+0x19B58` zeroed, as the window's *Clear Test School* does (`0x1740E2C`); on a level unload the barn does that itself (`0x174A308`). Function starts are checked before any call |
| **Shard fall** (experimental) | shards (1–8), distance around you, Drop / Remove | `ManualShardFall` with `spawnPositions` = a clump (`game:clumpBarn():CreateClumpWithSize(n)`, `SetAt(0…Size()-1, marker)`; out-of-range indexes abort the game, `0x14BD1B0`) of markers on a ring around you. Its start (`0x19C963C`) makes one `ShardFall` with those points and starts it; switched off it stops that one (`0x19CADF8`). Kept as `spShard`; Remove switches it off. The clump and markers aren't released (the switched-off event still points at the clump), at most 8 shard falls per level |
| **Star fall** (experimental) | stars (1–8), height, spread, speed, colour | `StarFallShower` with a clump of start points above you, `rail` nil (checked at `0x205DBDC`), `onFinalImpact` nil (checked at `0x1B03ABC`), `killOnGroundCollision`. It stops itself once every star has landed (`0x205E5AC`), so it's a one-shot: never kept or switched off, no Remove; its markers stay until the level changes (at most 6 per level) |
| **Darkstone rain** | radius, drops per second, *drains your light*, Rain / Remove | `DarkstoneRainRegion` with its `region` marker on you (scaled to the radius), `dropCollectibles` false, `playerEnergyDrain` 0 (0.05, the game's, when ticked). Never stops itself (`0x15CEAA8`); kept as `spRain`, Remove switches it off |
| **Fish school debug window** (Developer) | toggle | the game's `kFishSchoolDebug` byte `0x3197898` (TheVoid's table; 0 normally): the barn's update (`0x17560D0`) then opens the developers' *Fish Schools* window (`0x173F6E4`; the same Create / Clear calls) and draws each school's debug shapes (`0x1754460`). Off puts 0 back. Whether the window shows depends on the game drawing its debug UI |

Not offered (shown greyed with the reason): **Shardnado** (its start reads its `ShardnadoParams` object unchecked, `0x1DF5C94`; only level files make those), **ShardFall / MeteorFall** alone (their shard count `+0x60` is filled by the eruption system: 0 from Lua, nothing happens, `0x19CEA94`), **SpawnCrabs** (`CrabParams` read unchecked, `0x1553CD8`), **SpawnMotes / SpawnLiftMotes** (`MoteParams` passed to the mote barn, read unchecked at `0x1A125A4`), **NPCs / spirits** (`NpcBarn:CreateNpc` exists, but NPCs are set up by the barn's `OnLevelLoadLate`, `0x1AA9C68` → `0x1AAC670`, while the level loads; a later one isn't), **krill, dark creatures, dark dragons** (their attacks knock off winged light, which the server keeps), **collectible fish** (pickups that may change the account).

How events are started: the same Lua as the features (every setting at the game's default first, objects nil, then
the tab's values; markers from `game:markerBarn():CreateMarker(0)` set with `m:pos({x, y, z})`, never read back).
Your position is read natively (Teleport's avatar chain) and written into the script as numbers.

### Recorder

- **Hook**: the game's base Event start, `0x1717C8C`: the function the game's Lua `Event:Start` calls (its method record
  at `0x3192508` points straight at it), that `SetEnabled(true)` tail-calls, and that the game's own code calls when it
  starts events (1,192 calls and 114 tail calls in the library). It's hooked with Dobby the first time you press Start
  (never at load), after checking its first four instructions are 0.35.2's; otherwise the recorder refuses and says so
  in the Log. Stop only stops recording (the hook stays, doing nothing).
- **Which event**: every game object keeps its meta class id at `+8`; the game checks it against the meta class's id
  (`+0x38`) when it makes an event. Each type object's `+0x28` points at its meta class, whose ids the game gives out
  once at start-up, so the mod reads each type's id once when you press Start and looks the event's id up.
- **Values**: toggles, numbers, whole numbers, x/y, x/y/z, colours and 4-number settings whose place in the event is
  known (from the game's setting records), read when the event starts, so they're what the game used. Not recorded:
  text, choices, game objects, settings whose type is only guessed, and values that don't look like their type
  (they get the game's default on replay).
- **Safety**: the hook's handler only copies: no memory allocation, locks, logging or Lua. Every read goes through the
  mod's crash-safe read, into a fixed ring of 4,096 entries that the window empties every frame (also while the panel
  is collapsed); if the game outruns it, the oldest are dropped and the tab says "some missed". The list keeps the
  last 20,000 rows; consecutive starts with the same values are counted on one row. The recorded event objects are
  never read again later (the game may have deleted them). Replayed events aren't kept, so they can't be switched off
  from the mod.

**Server types** (account, purchases, inventory, friends, requests): fireable only after turning on
*Server types*, which is off at every start, because they send requests to whatever server the game is connected to.

## How it works

- Nothing from the game is stored here: `src/generated/lab_data.inc` holds only **addresses** (offsets from the start
  of libBootloader.so) and a 32-bit checksum of each name. Names, setting names and notes are read from the running
  game; the checksum shows whether the read worked (on another game version the offsets are wrong and it says so).
- `tools/gen_offsets.py libBootloader.so` (Python 3, numpy, capstone) makes that file. It finds every call to the
  0.35.2 function that registers a type by name (`0x29B4E5C`), and each type's setting records: name, position in the
  event, type handler (which says toggle, number, text, vector, colour, choice or object) and attributes (default
  value, description, object type). Choices are the option names registered with the same type handler. It also
  records each type's parent (the class tree: a type's parent accessor returns the parent's type object), so the mod can
  tell events from everything else; parents not resolvable from the file are read at runtime from the type object
  (+0x30).
- Game addresses used: game object pointer `0x33A5E80`, its Lua state at `+0x20`, the game's Lua runner `0x29ADFA4`,
  the per-frame function where Lua runs `0x17BB458`, the Event start the Recorder hooks `0x1717C8C`.

## Building

Android NDK r23c, CMake 3.22+, Ninja:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Canvas provides Dear ImGui at runtime (`libciphered.so`). To link, the build makes a link-only stand-in from the same
Dear ImGui commit Canvas uses; it isn't shipped. GitHub Actions builds every push and publishes a release on `main`
when `config/config.json`'s version is new.

## Third-party code

- [Dear ImGui](https://github.com/ocornut/imgui) (commit d3c3514a, as in Canvas), MIT: `third_party/imgui`
- [Dobby](https://github.com/jmpews/Dobby), Apache-2.0: `third_party/dobby` (prebuilt arm64 static library)
- TheVoid 0.1.2 by Soyu, Apache-2.0: used as a reference for which features to offer and how its menu is laid out,
  and for its table of feature names and Sky 0.35.1 addresses (`src/generated/void_offsets.inc`, ported to 0.35.2).
  No TheVoid code is copied. Licensed under the Apache License, Version 2.0
  (https://www.apache.org/licenses/LICENSE-2.0).

Sky: Children of the Light is © thatgamecompany. This mod isn't affiliated with thatgamecompany or Canvas. Use at
your own risk.

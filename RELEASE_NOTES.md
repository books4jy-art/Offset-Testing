Offset Lab v0.37.2 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**New: Skip spirit tree prerequisites (experimental)**
- Where: open Offset Lab → **Social** tab → scroll to the **SPIRIT TREES** section → switch on
  **"Skip spirit tree prerequisites (experimental)"**. It's off every time the game starts.
- What it does: in a spirit's tree you can select and buy any node directly, without buying the nodes before it
  first. Prices and currency are unchanged (buying still spends your candles); items you already own still show as
  owned. Applies to every spirit tree. Switch it off to put the game's normal check back.
- How: one instruction in the spirit shop's button code (0x1543EE0, "is the node before this one owned?") is
  skipped; the mod checks it's the expected 0.35.2 code before changing it.

**Also (0.37.1):** corrected descriptions for two Danger zone switches and the Other offsets table. Note: the
0.37.1 build already contained this spirit tree switch.

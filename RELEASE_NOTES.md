Offset Lab v0.37.3 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**Fixed: Skip spirit tree prerequisites (experimental)**
- Where: Offset Lab → **Social** tab → **SPIRIT TREES** → **"Skip spirit tree prerequisites (experimental)"**.
  It's off every time the game starts.
- 0.37.2 changed only the game's "this spirit has something to buy" check, not the nodes themselves, so locked
  nodes stayed locked. The game asks "can this node be bought?" in five places; the switch now changes all five
  so a node only has to be unowned.
- Switch it on, then close and reopen the spirit's tree.
- Buying still goes to the server, which may refuse a node whose lower nodes you don't own.
- If any of the five spots doesn't match Sky 0.35.2, nothing is changed and the switch stays off (see the log).

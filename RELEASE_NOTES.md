Offset Lab v0.37.4 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**Fixed again: Skip spirit tree prerequisites (experimental)**
- Where: Offset Lab → **Social** tab → **SPIRIT TREES** → **"Skip spirit tree prerequisites (experimental)"**.
  It's off every time the game starts.
- 0.37.3 still showed nodes locked: the tree also checks whether the node before it is owned, in five more
  places (availability, tapping a node, and three display spots). The switch now changes all ten.
- Switch it on **before** opening the spirit's tree (or close and reopen the tree).
- Buying still goes to the server, which may refuse a node whose lower nodes you don't own.
- If any of the ten spots doesn't match Sky 0.35.2, nothing is changed and the switch stays off (see the log).

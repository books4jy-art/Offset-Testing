Offset Lab v0.37.7 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**Spirit tree switch: seasonal spirits' tiers** (Social → SPIRIT TREES → "Skip spirit tree prerequisites")
- Your diagnostic log showed that seasonal spirit trees are locked by **tier** (the spirit's trust tier), not by
  the node before. The grayed nodes are the ones above your current tier.
- The switch now also changes the three tier checks (availability, the "tier locked" message, the node check)
  and turns on the game's own tier byte 0x34B4600 (the same one as "Show every friendship node unlocked"), which
  stops the menu graying nodes above your tier.
- Turn the switch on, then close and reopen the spirit's tree.
- Buying still goes to the server, which may refuse a node above your tier.

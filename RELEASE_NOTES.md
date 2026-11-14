Offset Lab v0.37.5 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**New: spirit tree diagnostic** (Social → SPIRIT TREES, under the switch)
- 0.37.3 and 0.37.4 changed the checks they were meant to (the log confirms it), but the tree still shows nodes
  locked, so the tree you open is drawn by code that hasn't been identified yet.
- Steps: tap **Start tree diagnostic** → open the spirit's tree → tap a locked node → go back to Offset Lab and tap
  **Log** → send the log. **Stop** removes the watches.
- It only counts calls to 13 game functions; it changes nothing.

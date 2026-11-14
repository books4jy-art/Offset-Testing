Offset Lab v0.37.6 for Sky 0.35.2 (Canvas / Sky Modloader, arm64). Experimental: see the README.

**Spirit tree diagnostic fixed** (Social → SPIRIT TREES)
- In 0.37.5, **Stop** threw the counts away before they were logged. Now **Stop** logs them first (and **Log**
  still logs without stopping).
- Watches 17 game functions now, including the spirit shop "viewed" functions and the season pass button.
- Steps: **Start tree diagnostic** → open the seasonal spirit's tree → tap the grayed node → back to Offset Lab →
  **Log** or **Stop** → send the log.

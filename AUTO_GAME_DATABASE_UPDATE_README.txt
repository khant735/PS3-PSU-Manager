Stage66 experimental PS3-native GameDB updater
Options -> UPDATE PS2 GAME DATABASE (ONLINE) triggers direct verified HTTPS download of GameDB-PS2 PS2.titles.json; Redump XML is supplementary and optional. A complete GameDB download is required; a failed download or parse preserves the old TSV. Old serials are retained when merging. No save files are modified.

IMPORTANT LIMITATIONS:
- Host C++ syntax checked, NOT compiled or tested on real PS3.
- Only manual Options-menu updating implemented. Background periodic checks / startup updates are NOT implemented.
- Actual upstream JSON shape and Redump response must be validated with real network downloads. Redump may serve a compressed DAT not XML; this is skipped.
- This code uses the app's libcurl dependency and installed CA bundle, but not its progress/resume UI.
- The PS2 game title lookup still operates on XPO title-resolution paths only.
- Full game catalogue is NOT bundled. App fetches it when user selects Update.
- GameDB may change release schema; parser rejects unsupported formats without touching installed DB.

PS3 PSU Manager Stage66 — Redump DAT integration

1. Obtain PS2 DAT including serial/version fields from https://redump.info/datfile/ps2/serial,version
2. Obtain GameDB PS2 titles JSON from https://github.com/niemasd/GameDB-PS2/releases/latest/download/PS2.titles.json
3. python tools_build_ps2_titles.py PS2.titles.json PS2_GAME_TITLES.tsv
4. python tools_merge_redump_ps2.py redump_ps2.dat PS2_GAME_TITLES.tsv
   (A ZIP containing Redump XML DAT is also accepted.)
5. Copy PS2_GAME_TITLES.tsv to /dev_hdd0/game/PSU000001/USRDIR/

Redump is supplementary: existing GameDB names take priority on serial collisions.
The importer requires actual serial data in the DAT; it will not guess serials from title.
This is a SOURCE-only update: no new PS3 ELF/SELF/PKG built, no upstream full DAT downloaded.
Existing app title lookup is restricted to XPO generated title lookup, not all browser rows.
Game opening remains read-only; no automatic conversion of XPO saves.
Redump source: https://redump.info/ ; GameDB-PS2: https://github.com/niemasd/GameDB-PS2

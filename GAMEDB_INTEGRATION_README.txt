PS3 PSU Manager Stage66 - external PS2 game title catalogue support

New: source/ps2_game_titles.c, include/ps2_game_titles.h.
Modified: source/main.cpp, XPO title resolution now checks the external TSV first.

Download the latest PS2.titles.json from:
https://github.com/niemasd/GameDB-PS2
Convert with:
python3 tools_build_ps2_titles.py PS2.titles.json PS2_GAME_TITLES.tsv

Install the resulting TSV on the PS3 at either:
/dev_hdd0/game/PSU000001/USRDIR/PS2_GAME_TITLES.tsv
/dev_hdd0/tmp/fakeusb/PS2/SAVEDATA/PS2_GAME_TITLES.tsv

The included TSV is a 3-entry SAMPLE only, not the full catalogue.
The full upstream JSON could not be downloaded in this execution environment.
GameDB serials are disc IDs and are not guaranteed to match every memory-card save directory.
This implementation currently affects XPO conversion's generated-title lookup only,
not every title shown in the file browser. Original XPO files remain unchanged.

Attribution: GameDB-PS2 by niemasd, sources include GameFAQs, Redump,
PlayStation Datacenter, MiSTer Addons and VGCollect. Check upstream terms
before redistribution. No PS3 ELF/SELF/PKG rebuilt for this change.

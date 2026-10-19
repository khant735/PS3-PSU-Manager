GameFAQs save HTML importer (experimental)

Use a locally saved GameFAQs PS2 saves webpage obtained through normal browser access.
Example:
python import_gamefaqs_html.py game_saves.html --page-url https://gamefaqs.gamespot.com/ps2/12345-game/saves --game "Game Name" --region Europe --output GAMEFAQS_SAVES.tsv

Only explicit GameFAQs-hosted links with recognized file extensions are included.
GameFAQs may use JavaScript, tokenized endpoints or CDN redirects: those entries will NOT be found. No automatic bulk discovery is claimed. Do not treat the generated TSV as complete.

This tool runs on a PC and generates input for the existing Stage66 Saves browser. It is not integrated into the PS3 executable. No PS3 binary was built.

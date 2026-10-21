# GameFAQs save catalogue — initial seed import

The GameFAQs save index is **separate** from the 12,944-record PS2 game-title database. A game title or serial does not establish a GameFAQs save URL.

## Bootstrapping an empty index

Place a UTF-8, tab-separated text file at:

`/dev_hdd0/game/PSU000001/USRDIR/GAMEFAQS_SEEDS.tsv`

Each non-comment line must contain exactly these three fields, separated by literal TAB characters:

```text
GAME TITLE<TAB>REGION<TAB>https://gamefaqs.gamespot.com/ps2/NNNNNN-game-slug/saves
```

Replace the placeholders with **actual GameFAQs save-listing URLs verified by you**; the example above is a format template, not a real listing. The accepted HTTPS hosts are `gamefaqs.gamespot.com` and `www.gamefaqs.com`. Game names are display labels, and regions are labels such as `USA` or `Europe`, not proof of the actual save's region.

Open **PS2 Saves — GameFAQs**, then press **Square** while the index is empty. Imported records are labelled **PAGE** and contain no download URL. Navigate to the game and region, then press **Square** to scan its listing for save-detail pages. Only explicit, permitted file links discovered from fetched pages are eligible for downloading. The existing index is merged, not cleared.

A failed HTTPS request, 403, unavailable page, or absent direct file link must **not** be interpreted as zero saves existing. The importer cannot automatically discover the full GameFAQs PS2 catalogue from the game-title database, and this mechanism does not bypass login, robots, or access restrictions.

This feature is experimental and must be verified on PS3/RPCS3.

# PS2 catalogue and save-source integration plan

This is a source registry and implementation contract, not a claim that these crawlers are implemented.

## Game/release identification
| Source | Seed | Use | State |
| --- | --- | --- | --- |
| GameDB-PS2 | https://github.com/niemasd/GameDB-PS2 | serial -> title; investigate full PS2.data.tsv | title import implemented |
| Redump | https://redump.info/datfile/ps2/serial,version | disc serial, revision and edition evidence | partial import; version details not yet parsed |
| SerialStation | https://serialstation.com/ | physical releases, serials, revisions | planned |
| PCSX2 Wiki | https://wiki.pcsx2.net/List_of_games | cross-reference titles and serials | planned |
| PlayStation DataCenter PAL | https://psxdatacenter.com/psx2/plist2.html | PAL release identifiers | planned |
| PlayStation DataCenter NTSC-U | https://psxdatacenter.com/psx2/ulist2.html | US release identifiers | planned |
| PlayStation DataCenter NTSC-J | https://psxdatacenter.com/psx2/jlist2.html | Japanese release identifiers | planned |
| LaunchBox | https://gamesdb.launchbox-app.com/platforms/games/48-sony-playstation-2 | titles, dates, alternate titles | planned |
| GameFAQs | https://gamefaqs.gamespot.com/ps2 | game identity and save index | planned |
| MobyGames / IGN / Giant Bomb / Wikipedia | their PS2 catalogue pages | secondary date/title verification | planned |
| Sega Retro | https://segaretro.org/ | supplementary Sega release evidence only | planned |

GameTDB is intentionally excluded as a PS2 catalogue source.

## Downloadable PS2 saves
| Source | Purpose | State |
| --- | --- | --- |
| GameFAQs | PS2 saves; original format, region, description and page link | existing index reader and detail-link extractor; catalogue builder not yet implemented |
| PS2 Save Tools | https://www.ps2savetools.com/downloads/saves/ | inspect actual downloadable files | planned |
| CodeJunkies / Datel Powersaves and Community Saves | historical completion and cheat-enhanced saves | research only; no verified live feed |
| CodeBreaker, GameShark, Xploder | historical save archives, not just cheat codes | research only |
| PS2-Home forum | https://www.ps2-home.com/forum/index.php | save/homebrew forum threads and attachments | access restricted: HTTP 403 observed 2026-10-20; do not claim links verified |\n| Internet Archive Wayback | https://web.archive.org/ | recover historical catalogue and attachment URLs | research only; verify attachment bytes separately |
| GameHacking.org | https://gamehacking.org/ | cheat codes and identifiers, NOT presumed downloadable saves | separate cheat-data candidate |

Swap Magic is not treated as a verified save-download catalogue.

## Data integrity requirements
- Keep a canonical game identity separate from release/edition/disc rows. A shared title alone is not sufficient to merge two games.
- Key release records by verified serial plus edition/revision/disc evidence; do not collapse distinct discs or regional releases.
- Never invent dates, countries, editions, formats, URLs, or download availability. Preserve source URL, observed date and date precision.
- Save entries must retain original format, source URL, author if known, region and descriptive progress/cheat state. Conversion to PSU must be separately validated.
- An HTTP challenge, CAPTCHA, Cloudflare page, redirect loop or rate limit is a source failure, never an empty catalogue.
- Respect site access rules and rate limits. Prefer official structured feeds. Do not bypass access controls.
- On parse/download/write failure preserve existing database files; stage new files before atomic replacement.
- Crawl incrementally on PS3 with bounded response size, checkpointing and deduplication; avoid unbounded RAM use.
- Test against saved fixtures before enabling a new provider; no untested provider should be reported as operational.

## Provider completion gate
A provider is **complete** only when discovery, parsing, bounded fetching, retry/backoff, durable checkpoints, provenance, deduplication, failure preservation, fixture tests, real-hardware validation and an installable build have been verified. Merely adding a URL or parser does not qualify.

### PS2-Home investigation
- Homepage retrieval returned HTTP 403 Forbidden on 2026-10-20 from the current automated environment.
- Cause is not established: forum permissions, WAF/Cloudflare, account requirements or server configuration may apply.
- Do not bypass anti-bot protection, automatically submit credentials or label inaccessible attachments as valid downloads.
- Capture only publicly accessible metadata, retain source links and mark inaccessible files as unverified.
- Archive snapshots may identify historical links, but the attachment itself must be retrievable and its content validated before offering it.

### Completion sequence
1. Validate and stabilize the existing GameDB/Redump importer with fixtures and actual builds.
2. Parse regional PSX DataCenter lists, then PCSX2 Wiki and SerialStation; maintain release/disc identity separately from canonical game identity.
3. Import richer GameDB release data, dates and revisions; reconcile conflicts without inventing fields.
4. Add LaunchBox and secondary metadata sources where access terms allow.
5. Build GameFAQs game/save discovery, incremental checkpoints and link verification; index save metadata without downloading files automatically.
6. Evaluate PS2 Save Tools and historical CodeJunkies/CodeBreaker/GameShark/Xploder archives; classify Wayback and PS2-Home failures accurately.
7. Integrate search, regional filtering, format conversion, validation and safe downloading in the PS3 UI.
8. Compile ELF/SELF/PKG, run fixture tests and hardware/emulator checks, publish only verified outputs.

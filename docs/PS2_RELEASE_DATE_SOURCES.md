# PS2 regional release-date research and import

The packaged PS2 release database contains one record per disc serial. GameDB-PS2 is the baseline; supplemental dates must be curated before import.

## Primary bulk research source

- [PCSX2 Wiki: List of games](https://wiki.pcsx2.net/List_of_games) — reports 12,793 unique Sony serials and 13,764 total serial entries (page updated 2024-03-16). Includes title, alternate title, language, region, serial and release-date columns. **Do not automatically assign a row's first date to all its serials:** some rows contain multiple editions and multiple dates, including demos and rereleases.
- [PSX DataCenter](https://psxdatacenter.com/) — individual serial-specific records.
- [GameDB-PS2](https://github.com/niemasd/GameDB-PS2) — baseline serial-indexed metadata.
- [Redump](https://redump.org/) — disc identification, regions and revisions; not a general release-date feed.

## Secondary cross-check sources

- [Push Square PS2](https://www.pushsquare.com/ps2/games/browse) — title-based regional dates; verify against serial-specific evidence.
- [MobyGames PS2](https://www.mobygames.com/platform/ps2/) — platform and regional release records.
- [GameFAQs PS2](https://gamefaqs.gamespot.com/ps2/) — title and regional release data.
- [IGN PS2](https://www.ign.com/games/platform/ps2), [GameSpot PS2](https://www.gamespot.com/platform/ps2/), [LaunchBox PS2](https://gamesdb.launchbox-app.com/platforms/games/48-sony-playstation-2), [Giant Bomb](https://www.giantbomb.com/), [Wikipedia PS2 A–K](https://en.wikipedia.org/wiki/List_of_PlayStation_2_games_(A%E2%80%93K)), [Wikipedia PS2 L–Z](https://en.wikipedia.org/wiki/List_of_PlayStation_2_games_(L%E2%80%93Z)).
- [Sega Retro PS2](https://segaretro.org/Category:PlayStation_2_games), [TCRF PS2](https://tcrf.net/Category:PlayStation_2_games), [RetroAchievements PS2](https://retroachievements.org/system/21-playstation-2/games), [PCSX2 compatibility](https://pcsx2.net/compat) — identification and supplementary cross-checks.
- Official regional publisher sites, archived announcements, Famitsu and Wayback Machine — historical evidence.

## Curated input

Create `data/ps2_release_candidates.tsv` with tab-separated headers:

```tsv
serial	release_date	region	source_url
```

Every candidate must have an exact serial, an ISO date (YYYY-MM-DD, YYYY-MM or YYYY), a matching region when supplied, and a provenance URL. Review ambiguous multi-serial/edition rows manually. Do not guess dates from title-only pages, substitute a release date from another region, or overwrite existing sourced dates.

Local validation:

```sh
python3 tools/merge_ps2_release_dates.py pkgfiles/USRDIR/PS2_GAME_TITLES.releases.tsv data/ps2_release_candidates.tsv --output /tmp/PS2_GAME_TITLES.releases.merged.tsv --review /tmp/PS2_GAME_TITLES.releases.review.tsv
```

The build workflow applies this optional file after downloading the GameDB baseline. If no curated file exists, the build uses the original GameDB data unchanged.

## Tracking

Before claiming additional coverage, record baseline dated count, candidate count, accepted new dates, rejected conflicts and final dated count from the build log. Research URLs alone are not imported data. Follow each website's access and reuse requirements.

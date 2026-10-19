GAMEFAQS SAVES CATALOGUE - STAGE66 PARSER CHECKPOINT

This is an experimental source module, NOT an integrated GameFAQs crawler.
The source contains a defensive TSV save-index parser, A-Z grouping, and
HTTPS URL checks. The PS3 catalogue menu, live indexing, and download
workflow have NOT been connected.

TSV columns (no header required):
game<TAB>region<TAB>format<TAB>description<TAB>GameFAQs page URL<TAB>direct file URL

Empty direct URL is permitted and must mean "open original GameFAQs page",
NOT "guess a file URL". A direct URL is accepted only when on the exact
GameFAQs host and ending with an approved PS2 save extension. Real GameFAQs
downloads may use redirects, CDN hosts, tokens, or restrictions; this module
does not claim to support them. Do not bypass anti-bot or access restrictions.
The source does not contain or invent any GameFAQs save entries.

Compile host tests:
gcc -std=c11 -Wall -Wextra -Werror -I source source/gamefaqs_saves_index.c tests/test_gamefaqs_saves.c -o test_gfs
./test_gfs

Next: validate GameFAQs access and source format; implement authorized
index ingestion; integrate Saves > 0-9 / A-Z > Game > Region > Format > Save
with existing downloader, preserving original formats and filenames.
Opening any save remains read-only. Downloads must be explicit.

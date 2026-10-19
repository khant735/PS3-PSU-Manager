from import_gamefaqs_html import extract
page='https://gamefaqs.gamespot.com/ps2/12345-game/saves'
s='<a href="/downloads/test.max">Save A</a><a href="https://evil.example/a.zip">Fake</a><a href="/ps2/12345-game/saves">Page</a><a href="/downloads/test.max">Duplicate</a>'
a=extract(s,'Example','Europe',page)
assert len(a)==1 and a[0].split('\t')[2]=='MAX'
assert extract('<a href="/ps2/12345-game/saves">Page</a>','Example','Europe',page)==[]
print('PASS: explicit links, deduplication, host checks, no invented URLs')

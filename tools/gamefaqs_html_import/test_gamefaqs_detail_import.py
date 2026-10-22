import unittest
from gamefaqs_detail_import import extract, extract_listing_entries
ROOT='https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves'
class Tests(unittest.TestCase):
 def test_listing(self):
  r=extract(ROOT,'<a href="/ps2/561292-silent-hill-3/saves/30435">Save</a>')
  self.assertEqual(r['detail_pages'][0][0],'30435');self.assertEqual(r['download_rows'],[])
 def test_detail(self):
  r=extract(ROOT+'/30435','<a href="/downloads/save.max">Download MAX</a>','Silent Hill 3','Europe')
  self.assertEqual(len(r['download_rows']),1);self.assertIn('\tMAX\t',r['download_rows'][0])
 def test_ico_saved_listing(self):
  page='https://gamefaqs.gamespot.com/ps2/367472-ico/saves'
  fixture='''<div class="head"><h2 class="title">PlayStation 2 PS3 Virtual Memory Card Save (ZIP) (Japan)</h2></div>
  <div class="body"><ol><li><div class="list_img"><a href="https://gamefaqs.gamespot.com/ps2/367472-ico/saves/35541" download="" rel="nofollow">Download</a></div>
  <div class="content"><div class="bold">From <a href="/community/760194962">760194962</a> (09/24/2026; 28KB)</div>
  <div class="meta">.psu, China mainland version, clear data</div></div></li></ol></div>
  <div class="head"><h2 class="title">PlayStation2 Max Drive Save (Europe)</h2></div>
  <div class="body"><ol><li><div class="list_img"><a href="/ps2/367472-ico/saves/12582" download="">Download</a></div>
  <div class="content"><div class="bold">From <a href="/community/lordvader159">lordvader159</a> (08/03/2006; 38KB)</div>
  <div class="meta">Completed twice</div></div></li></ol></div>'''
  rows=extract_listing_entries(page,fixture)
  self.assertEqual([x['save_id'] for x in rows],['35541','12582'])
  self.assertEqual(rows[0]['uploader'],'760194962')
  self.assertEqual(rows[0]['date'],'09/24/2026')
  self.assertEqual(rows[0]['size'],'28KB')
  self.assertIn('(Japan)',rows[0]['category'])
  self.assertEqual(rows[0]['description'],'.psu, China mainland version, clear data')
  self.assertFalse(rows[0]['download_verified'])
  self.assertEqual(extract(page,fixture)['download_rows'],[])
 def test_reject(self):
  with self.assertRaises(ValueError):extract('https://gamefaqs.gamespot.com.evil.org/ps2/561292-silent-hill-3/saves','')
  r=extract(ROOT,'<a href="https://evil.org/save.zip">bad</a><a href="/ps2/561292-silent-hill-3/saves/30435">OK</a>')
  self.assertEqual(len(r['detail_pages']),1);self.assertFalse(r['download_rows'])
if __name__=='__main__':unittest.main()

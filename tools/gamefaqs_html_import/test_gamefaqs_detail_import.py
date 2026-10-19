import unittest
from gamefaqs_detail_import import extract
ROOT='https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves'
class Tests(unittest.TestCase):
 def test_listing(self):
  r=extract(ROOT,'<a href="/ps2/561292-silent-hill-3/saves/30435">Save</a>')
  self.assertEqual(r['detail_pages'][0][0],'30435');self.assertEqual(r['download_rows'],[])
 def test_detail(self):
  r=extract(ROOT+'/30435','<a href="/downloads/save.max">Download MAX</a>','Silent Hill 3','Europe')
  self.assertEqual(len(r['download_rows']),1);self.assertIn('\tMAX\t',r['download_rows'][0])
 def test_reject(self):
  with self.assertRaises(ValueError):extract('https://gamefaqs.gamespot.com.evil.org/ps2/561292-silent-hill-3/saves','')
  r=extract(ROOT,'<a href="https://evil.org/save.zip">bad</a><a href="/ps2/561292-silent-hill-3/saves/30435">OK</a>')
  self.assertEqual(len(r['detail_pages']),1);self.assertFalse(r['download_rows'])
if __name__=='__main__':unittest.main()

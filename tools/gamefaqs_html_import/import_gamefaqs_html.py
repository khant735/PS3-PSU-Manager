#!/usr/bin/env python3
"""Extract explicit PS2 save-file anchors from locally saved GameFAQs HTML.
Does not crawl, guess URLs, or bypass access controls. Outputs Stage66 TSV.
"""
import argparse
import html
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin, urlparse, unquote
import re

FORMATS={'.psu':'PSU','.max':'MAX','.cbs':'CBS','.xps':'XPS','.sps':'SPS','.xpo':'XPO','.spo':'SPO','.psv':'PSV','.zip':'ZIP'}
HOSTS={'gamefaqs.gamespot.com','www.gamefaqs.com'}
class Anchors(HTMLParser):
 def __init__(self):
  super().__init__();self.anchors=[];self.href=None;self.text=[]
 def handle_starttag(self,tag,attrs):
  if tag=='a':self.href=dict(attrs).get('href');self.text=[]
 def handle_data(self,data):
  if self.href is not None:self.text.append(data)
 def handle_endtag(self,tag):
  if tag=='a' and self.href is not None:
   self.anchors.append((self.href,' '.join(''.join(self.text).split())))
   self.href=None;self.text=[]
def extract(source,game,region,page):
 u=urlparse(page)
 if u.scheme!='https' or u.hostname not in HOSTS or u.username or u.password:raise ValueError('Untrusted GameFAQs source URL')
 parser=Anchors();parser.feed(source)
 rows=[];seen=set()
 for href,description in parser.anchors:
  url=urljoin(page,html.unescape(href));v=urlparse(url)
  if v.scheme!='https' or v.hostname not in HOSTS or v.username or v.password:continue
  ext=Path(unquote(v.path)).suffix.lower()
  if ext not in FORMATS or url in seen:continue
  seen.add(url)
  description=description or Path(unquote(v.path)).name
  values=[game,region,FORMATS[ext],description[:180],page,url]
  rows.append('\t'.join(re.sub(r'[\t\r\n]+',' ',value) for value in values))
 return rows

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('html',type=Path);p.add_argument('--page-url',required=True);p.add_argument('--game',required=True);p.add_argument('--region',default='Unspecified');p.add_argument('--output',type=Path,default=Path('GAMEFAQS_SAVES.tsv'))
 a=p.parse_args();rows=extract(a.html.read_text(encoding='utf-8'),a.game,a.region,a.page_url)
 if rows:
  with a.output.open('a',encoding='utf-8') as f:f.write('\n'.join(rows)+'\n')
 print(f'{len(rows)} explicit save-file link(s) extracted; no links guessed')
if __name__=='__main__':main()

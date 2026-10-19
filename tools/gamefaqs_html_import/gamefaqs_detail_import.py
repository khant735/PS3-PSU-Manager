#!/usr/bin/env python3
"""Import user-saved GameFAQs save detail HTML into a conservative local index.
No network crawling or invented downloads. Explicit file links only.
"""
import argparse, html, re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin, urlparse, unquote

HOSTS={'gamefaqs.gamespot.com','www.gamefaqs.com'}
EXT={'.psu':'PSU','.max':'MAX','.cbs':'CBS','.xps':'XPS','.sps':'SPS','.xpo':'XPO','.spo':'SPO','.psv':'PSV','.zip':'ZIP'}
PAT=re.compile(r'^/ps2/(\d+)-([a-z0-9-]+)/saves(?:/(\d+))?/?$')
class Parser(HTMLParser):
 def __init__(self):
  super().__init__();self.anchors=[];self.href=None;self.words=[];self.title='';self.in_title=False
 def handle_starttag(self,tag,attrs):
  if tag=='a':self.href=dict(attrs).get('href');self.words=[]
  if tag=='title':self.in_title=True
 def handle_data(self,data):
  if self.href is not None:self.words.append(data)
  if self.in_title:self.title+=data
 def handle_endtag(self,tag):
  if tag=='a' and self.href is not None:
   self.anchors.append((self.href,' '.join(''.join(self.words).split())));self.href=None
  if tag=='title':self.in_title=False

def trusted(url):
 p=urlparse(url)
 return p.scheme=='https' and p.hostname in HOSTS and not p.username and not p.password and not p.port

def extract(page,source,game='',region='Unspecified'):
 if not trusted(page):raise ValueError('Invalid source host')
 match=PAT.fullmatch(urlparse(page).path)
 if not match:raise ValueError('Expected PS2 game saves or individual save page')
 gameid,slug,saveid=match.groups()
 parser=Parser();parser.feed(source)
 title=game or slug.replace('-',' ').title()
 seen=set();rows=[];details=[]
 for href,description in parser.anchors:
  url=urljoin(page,html.unescape(href))
  if not trusted(url):continue
  path=urlparse(url).path
  m=PAT.fullmatch(path)
  if m and m.group(1)==gameid and m.group(3) and not saveid:
   if url not in seen:
    seen.add(url);details.append((m.group(3),url,description))
   continue
  ext=Path(unquote(path)).suffix.lower()
  if ext not in EXT or url in seen:continue
  seen.add(url)
  label=description or Path(unquote(path)).name
  vals=[title,region,EXT[ext],label,page,url]
  rows.append('\t'.join(re.sub(r'[\t\r\n]+',' ',v) for v in vals))
 return {'game_id':gameid,'save_id':saveid,'game':title,'detail_pages':details,'download_rows':rows}

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('html',type=Path);ap.add_argument('--page-url',required=True)
 ap.add_argument('--game',default='');ap.add_argument('--region',default='Unspecified')
 ap.add_argument('--output',type=Path,default=Path('GAMEFAQS_SAVES.tsv'))
 args=ap.parse_args();result=extract(args.page_url,args.html.read_text(encoding='utf-8'),args.game,args.region)
 if result['download_rows']:
  with args.output.open('a',encoding='utf-8') as f:f.write('\n'.join(result['download_rows'])+'\n')
 print('Game ID:',result['game_id'],'Save ID:',result['save_id'] or '(listing)')
 print('Individual save pages:',len(result['detail_pages']))
 for ident,url,label in result['detail_pages']:print(' ',ident,url,label)
 print('Verified explicit file links:',len(result['download_rows']))
 if not result['download_rows']:print('No direct file links; catalogue must not show a Download action.')
if __name__=='__main__':main()

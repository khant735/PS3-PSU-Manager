#!/usr/bin/env python3
"""Convert GameDB-PS2 PS2.titles.json into PS2_GAME_TITLES.tsv.
Usage: python3 tools_build_ps2_titles.py PS2.titles.json PS2_GAME_TITLES.tsv
Input is downloaded separately from https://github.com/niemasd/GameDB-PS2
"""
import json, re, sys
from pathlib import Path
SERIAL = re.compile(r'^[A-Z]{4}-\d{5}$')
def convert(src, dst):
    data=json.loads(Path(src).read_text(encoding='utf-8'))
    if not isinstance(data,dict): raise ValueError('Expected serial-to-title JSON object')
    entries={}
    for key,value in data.items():
        serial=str(key).upper().replace('_','-').replace('.','')
        if not SERIAL.fullmatch(serial): continue
        if isinstance(value,str): title=value
        elif isinstance(value,dict): title=value.get('title') or value.get('name')
        else: continue
        if not isinstance(title,str):continue
        title=' '.join(title.split())
        if title: entries[serial]=title
    if not entries: raise ValueError('No valid serial/title pairs; inspect source schema')
    with open(dst,'w',encoding='utf-8',newline='\n') as out:
        for serial,title in sorted(entries.items()):out.write(f'{serial}\t{title}\n')
    return len(entries)
if __name__=='__main__':
    if len(sys.argv)!=3:sys.exit(__doc__)
    print(f'Exported {convert(sys.argv[1],sys.argv[2])} serial/title entries')

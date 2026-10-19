#!/usr/bin/env python3
"""Merge PS2 Redump DAT (XML or ZIP of XML) with GameDB-PS2 title TSV.
Usage: python tools_merge_redump_ps2.py REDUMP_DAT_OR_ZIP PS2_GAME_TITLES.tsv [OUTPUT.tsv]
Preserves existing GameDB title when both catalogues share a serial.
"""
import argparse
import re
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

SERIAL = re.compile(r"(?<![A-Z0-9])([A-Z]{4})[-_. ]?(\d{3})[._-]?(\d{2})(?!\d)",re.I)
EXACT = re.compile(r"^[A-Z]{4}-\d{5}$")

def serials(text):
    if not text: return set()
    return {f"{m.group(1).upper()}-{m.group(2)}{m.group(3)}" for m in SERIAL.finditer(text)}

def clean_title(name):
    name=re.sub(r"\s+", " ", name or "").strip()
    return name.replace("\t", " ")

def read_tsv(path):
    rows={}
    if not path.exists(): return rows
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if "\t" not in line: continue
        serial,title=line.split("\t",1)
        if EXACT.fullmatch(serial) and title.strip(): rows[serial]=clean_title(title)
    return rows

def parse_xml(stream):
    # Streaming parser: large DATs need not be loaded into memory.
    for _, elem in ET.iterparse(stream,events=("end",)):
        if elem.tag.rsplit("}",1)[-1].lower() not in ("game","machine"):continue
        title=clean_title(elem.attrib.get("name") or elem.attrib.get("description"))
        found=set()
        for node in elem.iter():
            tag=node.tag.rsplit("}",1)[-1].lower()
            if tag in ("serial","serials","serialnumber","serial_number","gameid","game_id"):
                found.update(serials(node.text))
            for key,value in node.attrib.items():
                if key.lower() in ("serial","serials","serialnumber","serial_number","gameid","game_id"):
                    found.update(serials(value))
                elif key.lower()=="name" and tag in ("rom","disk"):
                    found.update(serials(value))
        if title:
            for serial in found:yield serial,title
        elem.clear()

def redump_entries(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            members=[i for i in z.infolist() if i.filename.lower().endswith((".dat",".xml")) and not i.is_dir()]
            if not members:raise ValueError("ZIP has no DAT/XML files")
            for member in members:
                if member.file_size>150*1024*1024:raise ValueError("DAT too large")
                with z.open(member) as f:yield from parse_xml(f)
    else:
        with path.open("rb") as f:yield from parse_xml(f)

def merge(redump,base,output):
    if not redump.exists():raise FileNotFoundError(redump)
    rows=read_tsv(base)
    base_count=len(rows); discovered={}; conflicts=0
    for serial,title in redump_entries(redump):
        if serial in rows:
            if rows[serial]!=title:conflicts+=1
        else:
            discovered.setdefault(serial,title)
    if not discovered and not conflicts:raise ValueError("No Redump serials found; verify the DAT includes serial metadata")
    rows.update(discovered)
    data="".join(f"{serial}\t{title}\n" for serial,title in sorted(rows.items()))
    tmp=output.with_name(output.name+".part")
    tmp.write_text(data,encoding="utf-8")
    tmp.replace(output)
    return base_count,len(discovered),len(rows),conflicts

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("redump",type=Path)
    ap.add_argument("base",type=Path)
    ap.add_argument("output",type=Path,nargs="?")
    args=ap.parse_args()
    base,added,total,conflicts=merge(args.redump,args.base,args.output or args.base)
    print(f"GameDB/existing: {base}; new Redump serials: {added}; total: {total}; title conflicts retained from existing DB: {conflicts}")

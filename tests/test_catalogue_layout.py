#!/usr/bin/env python3
"""Host-side geometry regression for Phase 5P catalogue, no PS3 emulator needed."""
import math

def s125(n): return (n*5+2)//4

def textw(s,scale): return s125(len(s)*6*scale)

def face(scale): return max(18,s125(12*scale)+6)

def lineh(scale): return max(face(scale),s125(8*scale)+8)+8

def tokenw(tok,scale):
    if tok in ('X','O','SQUARE','TRIANGLE'): return face(scale)
    if tok=='UP/DOWN': return s125(28*scale)
    return textw(tok,scale)+18

controls=[('UP/DOWN','SELECT'),('L1/R1','CATEGORY'),('X','DOWNLOAD / SET URL'),('SQUARE','EDIT URL'),('O','BACK')]

def legend_lines(width,scale):
    lines=1; x=0
    for tok,action in controls:
        w=tokenw(tok,scale)+8+textw(action,scale)+24
        assert w<=width,(tok,w,width)
        if x and x+w>width: lines+=1;x=0
        x+=w
    return lines

def check(width,height,count,cat):
    title_scale,body_scale,help_scale=(3,2,2) if height>=1080 or width>=1600 else ((2,1,1) if height>=720 or width>=1200 else (1,1,1))
    w=min(width-40,1080)
    if w<200:w=width-8
    compact=height<400
    rowh=22 if compact else (36 if height>=720 else 25)
    header=60 if compact else (108 if height>=720 else 74)
    info_lines=1 if compact else 3
    info_step=s125(8*help_scale)+8
    legends=legend_lines(w-36,help_scale)
    top=8 if compact else 12
    gap=8 if compact else 14
    bottom=8 if compact else 18
    footer=top+info_lines*info_step+gap+legends*lineh(help_scale)+bottom
    avail=height-24-header-footer
    visible=min(count,max(1,avail//rowh))
    h=header+visible*rowh+footer
    y=max(4,(height-h)//2)
    assert y>=0 and y+h<=height-4,(width,height,count,h,y)
    list_end=y+header+visible*rowh
    info_y=list_end+top
    info_end=info_y+info_lines*info_step
    controls_y=info_end+gap
    assert controls_y>info_end
    assert controls_y+legends*lineh(help_scale)<=y+h-bottom
    assert info_y-list_end>=8
    if not compact:
        msgs=["Filen archive share - direct PSU URL not yet configured",
              "Direct PSU URL saved - X downloads and verifies PSU",
              "Source: ps2wiki.github.io / sysapps.html",
              "Filen shares require browser decryption; not direct .PSU links."]
        for i,msg in enumerate(msgs):
            scale=body_scale if i<2 else help_scale
            assert textw(msg,scale)<=w-40,(width,height,msg,textw(msg,scale),w-40)
    else:
        assert textw('Set direct .PSU URL to download',help_scale)<=w-40
    return f'{width}x{height} {cat}: {visible}/{count} rows, {legends} legend lines, info y={info_y}, legend y={controls_y}, panel y={y}..{y+h}'

for width,height in [(426,240),(640,480),(720,480),(720,576),(1280,720),(1920,1080),(1600,900)]:
    for count,cat in [(2,'games'),(5,'sysapps'),(9,'apps'),(5,'rtes')]:
        print(check(width,height,count,cat))
print('PASS: 28 populated-category layout cases, no footer/legend overlap')

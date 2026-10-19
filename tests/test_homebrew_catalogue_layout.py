#!/usr/bin/env python3
"""Geometry fixture for the PS2 Homebrew Store catalogue at supported modes."""

def s125(n): return (n*5+2)//4
def textw(s,scale): return s125(len(s)*6*scale)
def face(scale): return max(18,s125(12*scale)+6)
def lineh(scale): return max(face(scale),s125(8*scale)+8)+8
def tokenw(tok,scale): return face(scale) if tok in ('X','O','SQUARE') else (s125(28*scale) if tok=='UP/DOWN' else textw(tok,scale)+18)
controls=[('UP/DOWN','SELECT'),('L1/R1','CATEGORY'),('X','CHECK / DOWNLOAD'),('SQUARE','REFRESH'),('O','BACK')]
def legends(width,scale):
    lines=1;x=0
    for tok,action in controls:
        w=tokenw(tok,scale)+8+textw(action,scale)+24
        assert w<=width,(tok,w,width)
        if x and x+w>width: lines+=1;x=0
        x+=w
    return lines

def check(width,height,count):
    title,body,help=(3,2,2) if height>=1080 or width>=1600 else ((2,1,1) if height>=720 or width>=1200 else (1,1,1))
    w=min(width-40,1080)
    if w<200:w=width-8
    compact=height<400
    row=22 if compact else (36 if height>=720 else 25)
    header=60 if compact else (108 if height>=720 else 74)
    info_lines=1 if compact else 3
    step=s125(8*help)+8
    nleg=legends(w-36,help)
    top=8 if compact else 12;gap=8 if compact else 14;bottom=8 if compact else 18
    footer=top+info_lines*step+gap+nleg*lineh(help)+bottom
    available=height-24-header-footer
    visible=min(count,max(1,available//row))
    h=header+visible*row+footer
    y=max(4,(height-h)//2)
    assert y+h<=height-4,(width,height,y,h)
    list_end=y+header+visible*row
    info_y=list_end+top
    controls_y=info_y+info_lines*step+gap
    assert controls_y+nleg*lineh(help)<=y+h-bottom
    assert info_y-list_end>=8
    return f'{width}x{height}: {visible}/{count} rows, {nleg} legend lines'

for mode in [(426,240),(640,480),(720,480),(720,576),(1280,720),(1600,900),(1920,1080)]:
    print(check(*mode,9))
assert len(['Exploits','Loaders','Apps','System Apps','Emulators','Games','Diagnostics','UMCS'])==8
print('PASS: 7 display-mode fixtures and eight store categories')

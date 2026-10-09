#!/usr/bin/env python3
"""Generate full-screen native iPod-style M3X skins and original artwork.

Requires Pillow. Run, then `make -C build-m3x zip`. No click wheel.
"""
from pathlib import Path
from PIL import Image, ImageDraw
import math
import re

ROOT = Path(__file__).resolve().parents[2]
WPS = ROOT / 'rockbox/wps'
ART = WPS / 'm3x-ipod'
ART.mkdir(exist_ok=True)


def gradient(size, top, bottom):
    im = Image.new('RGB', size)
    d = ImageDraw.Draw(im)
    for y in range(size[1]):
        t = y / max(1, size[1]-1)
        c = tuple(round(a*(1-t)+b*t) for a,b in zip(top,bottom))
        d.line((0,y,size[0],y), fill=c)
    return im


screen = gradient((768,1280), (247,247,247), (230,230,230))
screen.paste(gradient((768,56),(253,253,253),(231,231,231)),(0,0))
screen.paste(gradient((768,80),(244,244,244),(199,199,199)),(0,56))
ImageDraw.Draw(screen).line((0,135,767,135),fill='#A5A5A5')
screen.save(ART / 'screen.bmp')
playing_screen = screen.copy()
ImageDraw.Draw(playing_screen).rounded_rectangle((48,694,719,705), radius=3, fill='#C4C4C4')
playing_screen.save(ART / 'playing-screen.bmp')
for name,top,bottom in (
    ('row-normal',(248,248,248),(237,237,237)),
    ('row-selected',(79,155,230),(39,95,165))):
    row=gradient((768,104),top,bottom)
    ImageDraw.Draw(row).line((0,103,767,103),fill='#CECECE' if name=='row-normal' else '#245B9A')
    row.save(ART / f'{name}.bmp')


def icon(index, color):
    im=Image.new('RGB',(144,144),'#FF00FF')
    d=ImageDraw.Draw(im)
    def line(p,w=4): d.line([tuple(round(v*3) for v in xy) for xy in p],fill=color,width=w*3)
    def box(p,fill=True): d.rectangle(tuple(round(v*3) for v in p),fill=color if fill else None,outline=color,width=3)
    def ellipse(p,fill=True): d.ellipse(tuple(round(v*3) for v in p),fill=color if fill else None,outline=color,width=6)
    def poly(p): d.polygon([tuple(round(v*3) for v in xy) for xy in p],fill=color)
    if index in (0,22):
        line([(21,34),(21,12),(37,8),(37,30)],4); line([(21,12),(37,8)],7)
        ellipse((9,29,23,39)); ellipse((25,25,39,35))
    elif index in (1,29): poly([(5,12),(20,12),(24,17),(43,17),(43,38),(5,38)])
    elif index in (2,10,12,15):
        for y in (12,24,36): ellipse((5,y-2,9,y+2)); line([(16,y),(42,y)],3)
    elif index in (4,25): poly([(13,7),(13,41),(40,24)])
    elif index in (8,17,19,20,23,24,31):
        ellipse((12,12,36,36))
        for angle in range(0,360,45):
            a=math.radians(angle); line([(24+13*math.cos(a),24+13*math.sin(a)),(24+20*math.cos(a),24+20*math.sin(a))],6)
        ellipse((19,19,29,29),False)
    elif index in (26,27):
        ellipse((15,15,33,33))
        for angle in range(0,360,45):
            a=math.radians(angle); line([(24+15*math.cos(a),24+15*math.sin(a)),(24+21*math.cos(a),24+21*math.sin(a))],2)
    elif index==30:
        for x,y in ((10,14),(24,32),(38,20)):
            line([(x,5),(x,43)],2); box((x-4,y-4,x+4,y+4))
    elif index in (9,14,18):
        box((9,9,39,39),False); line([(15,24),(33,24)],3); line([(24,15),(24,33)],3)
    elif index==21: ellipse((9,9,39,39))
    elif index in (11,28): ellipse((5,5,43,43),False); ellipse((19,19,29,29))
    else: box((10,7,37,41),False); line([(17,17),(30,17)],2); line([(17,25),(30,25)],2)
    return im.resize((48,48),Image.Resampling.NEAREST)


for name,color in (('icons-dark','#363636'),('icons-white','#FFFFFF')):
    atlas=Image.new('RGB',(48,48*32),'#FF00FF')
    for i in range(32): atlas.paste(icon(i,color),(0,i*48))
    atlas.save(ART / f'{name}.bmp')
for name,color in (('chevron-dark','#666666'),('chevron-white','#FFFFFF')):
    im=Image.new('RGB',(24,40),'#FF00FF');d=ImageDraw.Draw(im)
    d.line([(5,5),(19,20),(5,35)],fill=color,width=3)
    im.save(ART / f'{name}.bmp')
cover=gradient((456,456),(230,230,230),(215,215,215)); d=ImageDraw.Draw(cover)
d.rectangle((0,0,455,455),outline='#C3C3C3',width=2)
d.line([(248,302),(248,129),(330,111),(330,277)],fill='#999999',width=17)
d.ellipse((181,276,254,330),fill='#999999');d.ellipse((263,252,336,306),fill='#999999')
cover.save(ART / 'no-cover.bmp')
for name in ('previous','next','play','pause'):
    im=Image.new('RGB',(100,90),'#FF00FF');d=ImageDraw.Draw(im)
    if name=='pause':
        d.rounded_rectangle((28,16,42,74),radius=2,fill='#292929'); d.rounded_rectangle((58,16,72,74),radius=2,fill='#292929')
    else:
        for cx in ((50,) if name=='play' else (34,66)):
            direction=-1 if name=='previous' else 1
            d.polygon([(cx+direction*15,45),(cx-direction*13,20),(cx-direction*13,70)],fill='#292929')
    im.save(ART / f'{name}.bmp')
gradient((672,12),(82,158,231),(43,106,177)).save(ART / 'progress.bmp')

FONTS='''%Fl(2,27-Adobe-Helvetica.fnt)
%Fl(3,27-Adobe-Helvetica-Bold.fnt)
%Fl(4,35-Adobe-Helvetica.fnt)
%Fl(5,35-Adobe-Helvetica-Bold.fnt)
'''
STATUS='''%V(24,12,200,36,2)%Vf(202020)%Vb(ECECEC)
Rockbox
%V(626,12,112,36,2)%Vf(202020)%Vb(ECECEC)
%ar%bl%%
'''
SBS='''# Full-screen M3X iPod-style menu; original artwork, no click wheel.
%X(screen.bmp)
'''+FONTS+'''%V(0,0,768,1280,2)
%VB
%xl(page,screen.bmp,0,0)
%xd(page)
%T(0,56,144,80,cancel)
%?if(%cs,=,2)<|%Vd(title)%Vd(back)>
%V(0,0,768,1280,2)
%Vi(-,0,136,768,1144,1)%Vf(202020)%Vb(EEEEEE)
%Lb(row,768,104)
'''+STATUS+'''%Vl(title,156,80,456,48,5)%Vf(202020)%Vb(DDDDDD)
%ac%?if(%cs,=,1)<M3X|%?Lt<%Lt|Rockbox>>
%Vl(back,24,84,120,44,2)%Vf(444444)%Vb(DDDDDD)
%?if(%cs,=,1)<|Back>
%Vl(row,0,0,768,104,4)
%VB
%xl(N,row-normal.bmp,0,0)
%xl(S,row-selected.bmp,0,0)
%?Lc<%xd(S)|%xd(N)>
%Vl(row,32,28,48,48,4)
%xl(D,icons-dark.bmp,0,0,32)
%xl(W,icons-white.bmp,0,0,32)
%?if(%LI,>=,0)<%?Lc<%xd(W,%LI,1)|%xd(D,%LI,1)>>
%Vl(row,112,34,580,52,4)%Vb(EEEEEE)
%?Lc<%Vf(FFFFFF)|%Vf(202020)>%s%LT
%Vl(row,716,32,24,40,2)
%xl(C,chevron-dark.bmp,0,0)
%xl(B,chevron-white.bmp,0,0)
%?Lc<%xd(B)|%xd(C)>
'''
PLAYING='''# Full-screen M3X iPod-style now-playing screen.
%wd
%X(playing-screen.bmp)
'''+FONTS+'''%V(0,0,768,1280,2)
%T(0,56,144,80,menu)
%T(70,1056,180,144,wps_prev)
%T(70,1056,180,144,rwd,repeat_press)
%T(294,1056,180,144,play)
%T(518,1056,180,144,wps_next)
%T(518,1056,180,144,ffwd,repeat_press)
'''+STATUS+'''%V(156,80,456,48,5)%Vf(202020)%Vb(DDDDDD)
%acNow Playing
%V(24,84,120,44,2)%Vf(444444)%Vb(DDDDDD)
Home
%V(156,180,456,456,2)
%xl(cover,no-cover.bmp,0,0)
%Cl(0,0,456,456,c,c)
%?C<%Cd|%xd(cover)>
%V(48,694,672,16,2)%Vf(777777)%Vb(CBCBCB)
%pb(0,0,672,12,progress.bmp)
%T(0,0,672,16,progressbar)
%V(48,724,672,40,2)%Vf(333333)%Vb(EEEEEE)
%pc%ar-%pr
%V(40,802,688,52,5)%Vf(202020)%Vb(EEEEEE)
%s%ac%?it<%it|%fn>
%V(40,868,688,44,4)%Vf(444444)%Vb(EEEEEE)
%s%ac%?ia<%ia|Unknown artist>
%V(40,924,688,40,2)%Vf(666666)%Vb(EEEEEE)
%s%ac%?id<%id|Music>
%V(110,1082,100,90,2)
%xl(prev,previous.bmp,0,0)
%xd(prev)
%V(334,1082,100,90,2)
%xl(play,play.bmp,0,0)
%xl(pause,pause.bmp,0,0)
%?mp<%xd(play)|%xd(pause)|%xd(play)|%xd(pause)|%xd(pause)|%xd(play)>
%V(558,1082,100,90,2)
%xl(next,next.bmp,0,0)
%xd(next)
%V(40,1220,688,36,2)%Vf(777777)%Vb(E6E6E6)
%ac%pp of %pe
'''
(WPS/'m3x-ipod.768x1280x32.sbs').write_text(SBS)
(WPS/'m3x-ipod.768x1280x32.wps').write_text(PLAYING)
ENTRY='''<theme>
Name: m3x-ipod
Authors: Dorian Gironde; original full-screen M3X artwork
WPS: yes
SBS: yes
FMS: no
RWPS: no
RSBS: no
<main>
wps.768x1280x(16|24|32): m3x-ipod.768x1280x32.wps
sbs.768x1280x(16|24|32): m3x-ipod.768x1280x32.sbs
Font.768x1280x(16|24|32): 35-Adobe-Helvetica.fnt
backdrop: -
foreground color: 202020
background color: EEEEEE
line selector start color: 777777
line selector end color: 555555
line selector text color: FFFFFF
selector type: bar (gradient)
list separator height: off
statusbar: off
iconset: -
viewers iconset: -
show icons: on
ui viewport: -
filetype colours: -
</main>
</theme>'''
listing=WPS/'WPSLIST'; text=listing.read_text()
pattern=r'<theme>\s*Name: m3x-ipod\n.*?</theme>'
if re.search(pattern,text,re.S): text=re.sub(pattern,lambda _:ENTRY,text,flags=re.S)
else: text=text.rstrip()+'\n\n'+ENTRY+'\n'
listing.write_text(text)
print('Generated M3X full-screen iPod-style menus and playback skin')

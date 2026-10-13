"""Rebuild Downshift's original globe-and-download icon (Pillow required)."""
from pathlib import Path
from PIL import Image, ImageDraw
import math

root = Path(__file__).resolve().parents[1] / 'assets'
root.mkdir(exist_ok=True)
scale=4
image=Image.new('RGBA',(256*scale,256*scale),(0,0,0,0))
draw=ImageDraw.Draw(image)
def box(values): return tuple(round(v*scale) for v in values)
draw.ellipse(box((25,28,221,224)),fill='#6d1648')
draw.ellipse(box((23,20,219,216)),fill='#db2777')
for y in range(23,211):
    ratio=(y-23)/188
    color=(int(252-56*ratio),int(116-89*ratio),int(184-67*ratio),255)
    radius=math.sqrt(max(0,98**2-(y-118)**2))
    draw.line(box((121-radius,y,121+radius,y)),fill=color,width=scale)
draw.ellipse(box((61,21,181,215)),outline='#ffd6e9',width=3*scale)
draw.ellipse(box((94,21,148,215)),outline='#ffd6e9',width=3*scale)
for y in (65,116,165):
    radius=math.sqrt(98**2-(y-118)**2)
    draw.line(box((121-radius,y,121+radius,y)),fill='#ffd6e9',width=3*scale)
draw.arc(box((7,35,249,191)),185,345,fill='#8c1653',width=13*scale)
draw.arc(box((7,30,249,186)),185,345,fill='#fbcfe8',width=7*scale)
points=[(158,98),(194,98),(194,147),(219,147),(176,207),(133,147),(158,147)]
draw.polygon([box(p) for p in points],fill='#701747')
points=[(153,91),(189,91),(189,140),(214,140),(171,200),(128,140),(153,140)]
draw.polygon([box(p) for p in points],fill='white')
image=image.resize((256,256),Image.Resampling.LANCZOS)
image.save(root/'downshift.png')
(root/'downshift.rgba').write_bytes(image.tobytes())
image.save(root/'downshift.ico',bitmap_format='bmp',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
chevron=Image.new('RGBA',(56,56),(0,0,0,0))
ImageDraw.Draw(chevron).line([(12,20),(28,36),(44,20)],fill='#b94278',width=8,joint='curve')
small=chevron.resize((14,14),Image.Resampling.LANCZOS)
rows=[''.join('x' if small.getpixel((x,y))[3]>=70 else ' ' for x in range(14)) for y in range(14)]
(root/'chevron.xpm').write_text('/* XPM */\nstatic const char *chevron[] = {\n"14 14 2 1",\n"  c None",\n"x c #b94278",\n'+',\n'.join('"'+row+'"' for row in rows)+'\n};\n',encoding='ascii')

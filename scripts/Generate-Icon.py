"""Rebuild Downshift's original rounded download icon (Pillow required)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'assets'
root.mkdir(exist_ok=True)
scale=4
image=Image.new('RGBA',(256*scale,256*scale),(0,0,0,0))
draw=ImageDraw.Draw(image)
def box(values): return tuple(round(v*scale) for v in values)
# Original rounded download mark: one clear arrow, no globe detail.
mask=Image.new('L',image.size,0)
ImageDraw.Draw(mask).rounded_rectangle(box((14,14,242,242)),radius=52*scale,fill=255)
for y in range(256*scale):
    ratio=y/(256*scale-1)
    draw.line((0,y,256*scale,y),fill=(int(246-57*ratio),int(61-48*ratio),int(152-45*ratio),255))
image.putalpha(mask);draw=ImageDraw.Draw(image)
draw.rounded_rectangle(box((19,19,237,237)),radius=47*scale,outline=(255,255,255,65),width=2*scale)
draw.arc(box((42,47,214,219)),25,320,fill=(255,235,245,255),width=12*scale)
draw.polygon([box(p) for p in [(185,185),(205,187),(193,206)]],fill=(255,235,245,255))
arrow=[(105,54),(151,54),(151,129),(177,129),(128,184),(79,129),(105,129)]
draw.polygon([box(p) for p in arrow],fill='white')
image=image.resize((256,256),Image.Resampling.LANCZOS)
image.save(root/'downshift.png')
(root/'downshift.rgba').write_bytes(image.tobytes())
image.save(root/'downshift.ico',bitmap_format='bmp',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
chevron=Image.new('RGBA',(56,56),(0,0,0,0))
ImageDraw.Draw(chevron).line([(12,20),(28,36),(44,20)],fill='#b94278',width=8,joint='curve')
small=chevron.resize((14,14),Image.Resampling.LANCZOS)
rows=[''.join('x' if small.getpixel((x,y))[3]>=70 else ' ' for x in range(14)) for y in range(14)]
(root/'chevron.xpm').write_text('/* XPM */\nstatic const char *chevron[] = {\n"14 14 2 1",\n"  c None",\n"x c #b94278",\n'+',\n'.join('"'+row+'"' for row in rows)+'\n};\n',encoding='ascii')

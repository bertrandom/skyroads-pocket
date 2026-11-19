# The platform image (Platforms/_images/skyroads.bin), the picture the Pocket
# shows for SkyRoads in its list of platforms: the game's main menu, its logo
# over the start of the road, from the decompilation's art export (the menu
# at 4:3, decomp/art/menu-backgrounds/4x3/mainmenu.png).
#
# Analogue's format: 521x165, stored rotated 90 degrees counter-clockwise
# (165 pixels a row, 521 rows), 16 bits a pixel, the brightness in the first
# byte and 0 in the second (as in the Pocket's own images), no header.
#   python3 tools/mkplatimage.py MAINMENU.PNG OUT.bin
import sys
from PIL import Image, ImageOps

W, H = 521, 165
TOP = 24            # the strip's top in the 320x240 menu: just above the logo

src = Image.open(sys.argv[1]).convert("RGB")
h = round(src.width * H / W)
im = ImageOps.grayscale(src.crop((0, TOP, src.width, TOP + h))).resize((W, H), Image.LANCZOS)
rot = im.rotate(90, expand=True)            # counter-clockwise: 165x521
out = bytearray()
for v in rot.tobytes():
    out += bytes((v, 0))
assert len(out) == W * H * 2
open(sys.argv[2], "wb").write(out)

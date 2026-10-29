"""Rooftop dressing for T2 'Skyline'. Signs carry invented rune glyphs (no words, no fonts).
Run: blender -b --python rooftop.py"""
import sys, os, math, random; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omab_lib import *

def runes_on_face(prefix, slot, n, cell, rng, y, origin, vertical=True, width=.08):
    objs = rune_line(prefix, slot, n, cell, rng, vertical=vertical, width=width, depth=.06, origin=origin)
    for o in objs: o.location.y = y
    return objs

def helipad():
    o = [cyl("deck", "Dark", 8.0, .25, loc=(0, 0, .125), seg=8, bevel=.04), cyl("rim", "Chrome", 8.15, .12, loc=(0, 0, .06), seg=8)]
    for i in range(24):  # dashed touchdown ring
        a = 2 * math.pi * i / 24
        o.append(box(f"dash{i}", "Marking", (1.2, .22, .02), loc=(6.2 * math.cos(a), 6.2 * math.sin(a), .26), rot=(0, 0, a + math.pi / 2)))
    for i in range(16):
        a = 2 * math.pi * (i + .5) / 16
        o.append(cyl(f"edge{i}", "EmitAmber", .12, .1, loc=(7.75 * math.cos(a), 7.75 * math.sin(a), .3), seg=10))
    # Centre emblem: a big flat rune (invented), lying on the deck.
    rng = random.Random(77)
    for s, (a, b) in enumerate(glyph_strokes(rng)):
        p0, p1 = Vector(((a[0] - 1) * 1.6, (a[1] - 2) * 1.6, .27)), Vector(((b[0] - 1) * 1.6, (b[1] - 2) * 1.6, .27))
        d = p1 - p0; o.append(box(f"emb{s}", "Marking", (d.length + .35, .35, .02), loc=tuple((p0 + p1) / 2), rot=(0, 0, math.atan2(d.y, d.x))))
    return bake(o, "SM_Helipad")

def hvac():
    o = [box("body", "Paint", (3.0, 2.0, 1.5), loc=(0, 0, .85), bevel=.05), box("skid", "Dark", (3.3, 2.2, .15), loc=(0, 0, .07))]
    for i in range(14): o.append(box(f"fin{i}", "Dark", (.04, 2.04, 1.1), loc=(-1.3 + i * .2, 0, .85)))
    for x in (-.75, .75):
        o.append(cyl(f"fan{x}", "Dark", .62, .1, loc=(x, 0, 1.62), seg=32))
        o.append(cyl(f"guard{x}", "Chrome", .68, .06, loc=(x, 0, 1.7), seg=32, bevel=.01))
        for k in range(5): o.append(box(f"blade{x}{k}", "Chrome", (1.2, .05, .02), loc=(x, 0, 1.73), rot=(0, 0, k * math.pi / 5)))
    o.append(box("duct", "Paint", (.6, .6, 1.4), loc=(1.75, .5, .7), bevel=.03))
    return bake(o, "SM_HVAC")

def antenna():
    o = [cyl("mast", "Chrome", .09, 14, loc=(0, 0, 7), seg=12), cyl("base", "Dark", .5, .4, loc=(0, 0, .2), seg=12, bevel=.03)]
    for i, z in enumerate((4, 7, 10, 12.5)):
        w = 2.4 - i * .45
        o.append(box(f"xbar{i}", "Chrome", (w, .06, .06), loc=(0, 0, z)))
        o.append(box(f"ybar{i}", "Chrome", (.06, w * .7, .06), loc=(0, 0, z + .3)))
    o.append(cyl("dish", "Paint", .9, .25, loc=(.5, 0, 9), rot=(0, math.pi / 2.4, 0), seg=24, r2=.25))
    for z in (14.1, 9.6): o.append(cyl(f"beacon{z}", "EmitRed", .14, .22, loc=(0, 0, z), seg=12))
    return bake(o, "SM_Antenna")

def railing():
    o = []
    for x in (-2, 0, 2): o.append(cyl(f"post{x}", "Chrome", .035, 1.1, loc=(x, 0, .55), seg=10))
    o.append(cyl("top", "Chrome", .04, 4.0, loc=(0, 0, 1.1), rot=(0, math.pi / 2, 0), seg=10))
    o.append(cyl("mid", "Chrome", .025, 4.0, loc=(0, 0, .6), rot=(0, math.pi / 2, 0), seg=10))
    o.append(box("kick", "Dark", (4.0, .03, .15), loc=(0, 0, .08)))
    return bake(o, "SM_Railing")

def tank():
    o = [cyl("shell", "Paint", 2.0, 3.6, loc=(0, 0, 3.8), seg=32), cyl("roof", "Paint", 2.1, 1.0, loc=(0, 0, 6.1), seg=32, r2=.15)]
    for z in (2.4, 3.8, 5.2): o.append(cyl(f"band{z}", "Dark", 2.04, .1, loc=(0, 0, z), seg=32))
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        o.append(box(f"leg{k}", "Dark", (.2, .2, 2.2), loc=(1.4 * math.cos(a), 1.4 * math.sin(a), 1.1)))
    o.append(box("platform", "Dark", (4.2, 4.2, .12), loc=(0, 0, 2.0)))
    for z in range(9): o.append(box(f"rung{z}", "Chrome", (.03, .5, .03), loc=(2.05, 0, 2.2 + z * .4)))
    return bake(o, "SM_WaterTank")

def sign_vertical(name, seed, n=5):
    rng = random.Random(seed); H = n * 1.5 + .6
    o = [box("back", "Dark", (1.6, .3, H), loc=(0, 0, H / 2), bevel=.03), box("trim", "Chrome", (1.7, .26, H + .1), loc=(0, .03, H / 2))]
    for z in (0, H): o.append(box(f"cap{z}", "Chrome", (1.8, .4, .12), loc=(0, 0, z)))
    o += runes_on_face("r", "EmitGlyph", n, 1.0, rng, -.18, (-.5, H - .55))
    return bake(o, name)

def sign_horizontal(name, seed, n=7):
    rng = random.Random(seed); W = n * .9 * 1.2 + .6
    o = [box("back", "Dark", (W, .3, 1.9), loc=(W / 2 - .4, 0, 1.0), bevel=.03), box("trim", "Chrome", (W + .1, .26, 2.0), loc=(W / 2 - .4, .03, 1.0))]
    o += runes_on_face("r", "EmitGlyph", n, 1.2, rng, -.18, (0, .4), vertical=False)
    for x in (.4, W - 1.2): o.append(box(f"leg{x}", "Dark", (.15, .15, 2.0), loc=(x, .2, -1.0)))
    return bake(o, name)

def billboard():
    rng = random.Random(9)
    o = [box("frame", "Dark", (12.4, .5, 6.4), loc=(0, .3, 6), bevel=.05), box("screen", "Screen", (12, .1, 6), loc=(0, 0, 6))]
    for x in (-4, 4):
        o.append(box(f"leg{x}", "Dark", (.4, .4, 3.0), loc=(x, .5, 1.5)))
        o.append(box(f"brace{x}", "Chrome", (.12, .12, 3.4), loc=(x + .9, .5, 1.6), rot=(0, .5, 0)))
    o.append(box("walk", "Dark", (12.6, 1.2, .1), loc=(0, -.4, 2.9)))
    for i in range(12): o.append(cyl(f"lamp{i}", "EmitWhite", .07, .25, loc=(-5.5 + i, -.8, 3.0), rot=(.5, 0, 0), seg=8))
    o += runes_on_face("r", "EmitAmber", 8, .7, rng, -.1, (-3.3, 3.25), vertical=False, width=.06)
    return bake(o, "SM_Billboard")

if __name__ == "__main__":
    for f in (helipad, hvac, antenna, railing, tank, billboard):
        reset(); o = f(); export(o, o.name)
    for nm, sd, fn in (("SM_SignV1", 11, sign_vertical), ("SM_SignV2", 23, sign_vertical), ("SM_SignH1", 37, sign_horizontal)):
        reset(); export(fn(nm, sd), nm)

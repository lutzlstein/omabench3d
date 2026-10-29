"""Street-market dressing + drone for T3 'Downpour'. Original designs; sign text is invented runes.
Run: blender -b --python market.py"""
import sys, os, math, random; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omab_lib import *

def drone():
    # Compact quad drone: flat hex body, 4 arms, ducted rotor rings, cyan running light, red tail light.
    o = [cyl("body", "Paint", .22, .09, loc=(0, 0, 0), seg=6, bevel=.015), box("pod", "Dark", (.2, .12, .07), loc=(.1, 0, -.07), bevel=.01),
         cyl("eye", "EmitBlue", .035, .03, loc=(.21, 0, -.07), rot=(0, math.pi / 2, 0), seg=12)]
    for i in range(4):
        a = math.pi / 4 + i * math.pi / 2; x, y = .32 * math.cos(a), .32 * math.sin(a)
        o.append(box(f"arm{i}", "Dark", (.34, .04, .025), loc=(x / 2, y / 2, 0), rot=(0, 0, a)))
        o.append(cyl(f"duct{i}", "Chrome", .15, .05, loc=(x, y, .01), seg=20, r2=.15))
        o.append(cyl(f"hub{i}", "Dark", .03, .06, loc=(x, y, .02), seg=8))
        o.append(box(f"blade{i}", "Dark", (.26, .025, .006), loc=(x, y, .04), rot=(0, 0, a * 1.7)))
    o.append(cyl("tail", "EmitRed", .025, .02, loc=(-.2, 0, .03), seg=8))
    return bake(o, "SM_Drone")

def stall(seed):
    # Market stall: counter + slanted awning on posts, crates, hanging lamp, small rune placard.
    rng = random.Random(seed)
    w = rng.choice((2.4, 3.0, 3.6)); d = 1.6
    o = [box("counter", "Paint", (w, .6, 1.0), loc=(0, -d / 2 + .3, .5), bevel=.02), box("top", "Chrome", (w + .1, .7, .05), loc=(0, -d / 2 + .3, 1.02)),
         box("back", "Dark", (w, .08, 2.4), loc=(0, d / 2, 1.2))]
    for sx in (-1, 1):
        o.append(cyl(f"post{sx}", "Chrome", .04, 2.6, loc=(sx * (w / 2 - .05), -d / 2 + .05, 1.3), seg=10))
    o.append(box("awning", "Paint", (w + .4, d + .6, .04), loc=(0, -.1, 2.55), rot=(.18, 0, 0)))
    for i in range(int(w / .3)):  # awning stripes catch light
        o.append(box(f"str{i}", "Dark", (.12, d + .62, .042), loc=(-w / 2 + .15 + i * .3, -.1, 2.551), rot=(.18, 0, 0)))
    for i in range(rng.randint(3, 6)):
        s = rng.uniform(.35, .55)
        o.append(box(f"crate{i}", "Dark", (s, s * .8, s * .7), loc=(rng.uniform(-w / 2 + .3, w / 2 - .3), rng.uniform(-.2, .5), s * .35), bevel=.01))
    o.append(cyl("lampw", "Dark", .01, .4, loc=(0, -.2, 2.25), seg=6))
    o.append(cyl("lamp", "EmitAmber", .09, .12, loc=(0, -.2, 2.0), seg=14))
    o += runes_face("pl", "EmitGlyph", 3, .28, rng, origin=(-.4, 2.15), y=-d / 2 + .25)
    return bake(o, f"SM_Stall{seed}")

def runes_face(prefix, slot, n, cell, rng, origin, y):
    objs = rune_line(prefix, slot, n, cell, rng, vertical=False, width=.04, depth=.03, origin=origin)
    for x in objs: x.location.y = y
    return objs

def vent():
    o = [box("box", "Dark", (1.2, 1.2, .35), loc=(0, 0, .175), bevel=.02)]
    for i in range(9): o.append(box(f"slat{i}", "Chrome", (1.1, .05, .04), loc=(0, -.5 + i * .125, .36)))
    o.append(cyl("pipe", "Chrome", .12, .8, loc=(.45, .45, .5), seg=14))
    return bake(o, "SM_Vent")

def overpass():
    # 32 m elevated roadway span: deck, girders, barrier, underside light strips (centred on origin).
    L, W = 32.0, 7.0
    o = [box("deck", "Dark", (L, W, .7), loc=(0, 0, 0), bevel=.03)]
    for y in (-2.5, 0, 2.5): o.append(box(f"gird{y}", "Paint", (L, .4, 1.0), loc=(0, y, -.85)))
    for s in (-1, 1): o.append(box(f"barrier{s}", "Paint", (L, .25, .9), loc=(0, s * (W / 2 - .12), .8)))
    for i in range(16): o.append(box(f"ls{i}", "EmitWhite", (1.2, .12, .04), loc=(-L / 2 + 1 + i * 2, s * 1.2, -1.37)))
    for i in range(16): o.append(box(f"lt{i}", "EmitAmber", (1.2, .12, .04), loc=(-L / 2 + 1 + i * 2, -1.2, -1.37)))
    return bake(o, "SM_Overpass")

if __name__ == "__main__":
    for f in (drone, vent, overpass):
        reset(); o = f(); export(o, o.name)
    for sd in (3, 8, 15):
        reset(); o = stall(sd); export(o, o.name)

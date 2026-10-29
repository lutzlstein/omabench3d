"""Flying vehicles: SM_Spinner (police), SM_CarA (civilian coupe), SM_CarB (heavy hauler).
Original designs, 'inspired-by' a tapered police flyer: long wedge-tapered body, four wheel-pod nacelles,
bubble canopy, roof light bar. Run: blender -b --python vehicles.py"""
import sys, os, math; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omab_lib import *

def vehicle(name, L=5.6, W=1.1, H=0.42, exp=3.8, pods=4, pod_r=0.3, lightbar=True, canopy=(1.5, -1.2, 0.62, 0.27), tail=True, seed=1, cz=0.0):
    s = L / 5.6; objs = []
    # Body stations nose(+X) -> tail(-X): (x, halfW, halfH, zCentre, bellyFlatten)
    prof = [(2.95, .20, .06, .46, .5), (2.60, .52, .13, .49, .5), (2.10, .80, .22, .55, .5), (1.40, .98, .31, .62, .4),
            (0.50, 1.07, .40, .69, .4), (-0.40, 1.10, .43, .73, .4), (-1.30, 1.07, .44, .77, .4), (-2.05, .98, .40, .81, .4),
            (-2.55, .82, .30, .86, .4), (-2.85, .55, .16, .90, .4)]
    st = [(x * s, w * W / 1.1, h * H / .42, z, f) for x, w, h, z, f in prof]
    objs.append(subsurf(loft(st, n=24, exp=exp, name=name + "_body", slot="Paint"), 1))
    # Dark belly skirt + chrome waist trim (beltline) for silhouette breakup.
    trim = [(x, w * 1.012, h * 0.16, z + h * 0.05) for x, w, h, z, f in st[1:-1]]
    objs.append(subsurf(loft(trim, n=16, exp=4, name=name + "_trim", slot="Chrome"), 1))
    # Canopy: teardrop bubble on the roof.
    cx0, cx1, cw, ch = canopy
    zt = 0.69 + H * 0.95 + cz
    zr = 0.77 + H * 1.04   # rear-deck top for the light bar
    cst = [(cx0 * s, .08, .03, zt), ((cx0 * .55 + cx1 * .45) * s, cw, ch * .9, zt), ((cx0 * .2 + cx1 * .8) * s, cw * .92, ch, zt), (cx1 * s, .12, .05, zt + .05)]
    objs.append(subsurf(loft(cst, n=20, exp=2.2, name=name + "_canopy", slot="Glass"), 2))
    objs.append(subsurf(loft([(x, w * 1.04, .025, z - .01) for x, w, h, z in cst], n=20, exp=2.2, name=name + "_cframe", slot="Chrome"), 1))
    # Wheel-pod nacelles: short faired pods hanging off the flanks with a glowing thrust ring underneath.
    px = [1.75, -1.75] if pods == 4 else [-1.9]
    for i, x in enumerate(px):
        for sy in (-1, 1):
            y = sy * (W * 0.92 + pod_r * 0.15)
            pst = [(x * s + pod_r * 1.6, .05, .05, .48), (x * s + pod_r * 1.1, pod_r * .75, pod_r * .75, .5),
                   (x * s, pod_r, pod_r, .52), (x * s - pod_r * 1.2, pod_r * .9, pod_r * .85, .54), (x * s - pod_r * 1.7, .08, .08, .56)]
            pod = loft(pst, n=20, exp=2.4, name=f"{name}_pod{i}{sy}", slot="Paint"); pod.location.y = y; objs.append(subsurf(pod, 2))
            objs.append(cyl(f"{name}_ring{i}{sy}", "EmitBlue" if lightbar else "EmitAmber", pod_r * .62, .04, loc=(x * s, y, .52 - pod_r * .92), seg=24))
            objs.append(cyl(f"{name}_hub{i}{sy}", "Chrome", pod_r * .7, .12, loc=(x * s, y + sy * pod_r * .9, .52), rot=(math.pi / 2, 0, 0), seg=24, bevel=.02))
            objs.append(box(f"{name}_strut{i}{sy}", "Dark", (pod_r * 1.2, pod_r * 1.2, .1), loc=(x * s, sy * (W * .9), .55), bevel=.03))
    # Lights: headlight slits, tail bar, side marker strip, roof light bar (police).
    for sy in (-1, 1):
        objs.append(box(f"{name}_head{sy}", "EmitWhite", (.18, .22, .035), loc=(2.62 * s, sy * .32 * W / 1.1, .6), rot=(0, .2, sy * .25)))
        objs.append(box(f"{name}_side{sy}", "EmitWhite" if lightbar else "EmitAmber", (2.6 * s, .015, .02), loc=(-.2 * s, sy * W * 1.0, .74)))
    objs.append(box(f"{name}_tail", "EmitRed", (.03, 1.2 * W, .05), loc=(-2.83 * s, 0, .92), bevel=.01))
    if tail:
        objs.append(box(f"{name}_fin", "Paint", (.55 * s, 1.7 * W, .035), loc=(-2.5 * s, 0, 1.08), rot=(0, -.08, 0), bevel=.02))
        for sy in (-1, 1): objs.append(box(f"{name}_finpost{sy}", "Dark", (.25, .05, .28), loc=(-2.4 * s, sy * .55 * W, 1.0), bevel=.01))
    if lightbar:
        objs.append(box(name + "_lbbase", "Dark", (.5, .9, .06), loc=(-1.75 * s, 0, zr), bevel=.015))
        objs.append(box(name + "_lbR", "EmitRed", (.4, .38, .07), loc=(-1.75 * s, -.22, zr + .06), bevel=.015))
        objs.append(box(name + "_lbB", "EmitBlue", (.4, .38, .07), loc=(-1.75 * s, .22, zr + .06), bevel=.015))
    o = bake(objs, name)
    return o

if __name__ == "__main__":
    reset()
    export(vehicle("SM_Spinner"), "SM_Spinner")
    reset()
    export(vehicle("SM_CarA", L=4.6, W=1.0, H=0.38, exp=3.0, pods=2, pod_r=.42, lightbar=False, canopy=(1.0, -1.6, .64, .33), tail=False), "SM_CarA")
    reset()
    export(vehicle("SM_CarB", L=6.4, W=1.25, H=0.62, exp=4.5, pods=4, pod_r=.44, lightbar=False, canopy=(2.0, .6, .7, .3), tail=True, cz=-0.22), "SM_CarB")

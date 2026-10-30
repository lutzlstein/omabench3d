"""CC0 kit: builds the T2/T3 vehicles and props from public-domain (CC0 1.0) source assets
(Poly Haven models, ambientCG materials, OpenGameArt vehicles; see art/THIRD_PARTY.md), keeping the
mesh names/slots the game already binds. Sources are fetched by tools/fetch-cc0.sh into art/cc0/src.
Slots named X_<material> keep their own CC0 textures (assigned in UE by Scripts/make_cc0_materials.py,
listed in art/export/cc0_slots.json); classic slots (Paint/Glass/Emit*/...) are still bound at runtime.
Run: blender -b --python cc0_kit.py"""
import shutil
import sys, os, json, math, glob, random, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy, bmesh
from mathutils import Matrix, Vector
from omab_lib import reset, mat, box, cyl, bake, export, EXPORT
import rooftop, market

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cc0", "src"))
SLOTS = {}   # X_slot -> texture set

def _ph_tex(aid, name, key):
    d = os.path.join(ROOT, "ph", aid, "textures")
    for pat in (f"{name}_{key}_1k.jpg", f"{aid}_{key}_1k.jpg"):
        p = os.path.join(d, pat)
        if os.path.exists(p): return p
    return None

def _register_ph(aid, gl, m):
    """Texture set for a Poly Haven glTF material (base/arm/normal, mask for alpha-blended ones)."""
    def uri(t): return os.path.join(ROOT, "ph", aid, gl["images"][gl["textures"][t["index"]]["source"]]["uri"]) if t else None
    pbr = m.get("pbrMetallicRoughness", {}); n = m["name"]
    s = {"base": _ph_tex(aid, n, "diff") or uri(pbr.get("baseColorTexture")), "arm": _ph_tex(aid, n, "arm") or uri(pbr.get("metallicRoughnessTexture")),
         "nor": _ph_tex(aid, n, "nor_gl") or uri(m.get("normalTexture")), "mask": None, "src": f"https://polyhaven.com/a/{aid}"}
    for k in ("opacity", "alpha"):          # fetched separately (tools/fetch-cc0.sh)
        c = os.path.join(ROOT, "ph", aid, "textures", f"{n}_{k}_1k.jpg")
        if os.path.exists(c):
            s["mask"] = c
            if s["base"] and "opacity" in os.path.basename(s["base"]): s["base"] = _ph_tex(aid, n, "diff")
            break
    SLOTS["X_" + n] = s

def ph(aid, only=None, skip=()):
    """Import a Poly Haven glTF; returns its mesh objects (node names filtered), materials renamed X_<name>."""
    gpath = os.path.join(ROOT, "ph", aid, aid + ".gltf"); gl = json.load(open(gpath))
    before = set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=gpath)
    new = [o for o in bpy.data.objects if o not in before]
    for m in gl.get("materials", []): _register_ph(aid, gl, m)
    keep = []
    for o in new:
        base = o.name.split(".")[0]
        if o.type != "MESH" or (only and not any(base == k or base.startswith(k) for k in only)) or any(k in base for k in skip):
            continue
        keep.append(o)
    bpy.context.view_layer.update()
    for o in keep:
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
        for i, ms in enumerate(o.material_slots):
            if ms.material: ms.material = xmat(ms.material.name.split(".")[0])
    for o in new:
        if o not in keep: bpy.data.objects.remove(o)
    return keep

def xmat(n):
    n = "X_" + n if not n.startswith("X_") else n
    return bpy.data.materials.get(n) or bpy.data.materials.new(n)

def acg(aid, tile=1.0):
    """ambientCG material -> slot X_acg_<id>; packs AO/Roughness/Metalness into one ARM jpg (like Poly Haven)."""
    d = os.path.join(ROOT, "acg", aid); f = lambda k: (glob.glob(os.path.join(d, f"*_{k}.jpg")) or [None])[0]
    arm = os.path.join(d, f"{aid}_ARM.jpg")
    if not os.path.exists(arm):
        ao, ro, me = f("AmbientOcclusion"), f("Roughness"), f("Metalness")
        size = subprocess.check_output(["magick", "identify", "-format", "%wx%h", ro]).decode()
        ch = lambda p, v: ["(", p, "-colorspace", "gray", ")"] if p else ["(", "-size", size, f"xc:gray({v}%)", ")"]
        subprocess.check_call(["magick", *ch(ao, 100), *ch(ro, 50), *ch(me, 0), "-combine", "-quality", "92", arm])
    def clean(k, out):   # ambientCG names contain '-', which UE asset names reject
        p = os.path.join(d, f"{aid}_{out}.jpg")
        if not os.path.exists(p): shutil.copyfile(f(k), p)
        return p
    SLOTS["X_acg_" + aid] = {"base": clean("Color", "Base"), "arm": arm, "nor": clean("NormalGL", "Nor"), "mask": None, "tile": tile, "src": f"https://ambientcg.com/a/{aid}"}
    return "X_acg_" + aid

def xf(objs, loc=(0, 0, 0), rz=0.0, s=1.0, sv=None):
    """Move a group of objects: scale (uniform s or per-axis sv), rotate about Z (degrees), translate."""
    S = Matrix.Diagonal((*(sv or (s, s, s)), 1.0))
    M = Matrix.Translation(Vector(loc)) @ Matrix.Rotation(math.radians(rz), 4, "Z") @ S
    for o in objs: o.matrix_world = M @ o.matrix_world
    return objs

def bounds(objs):
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    return Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)])

def ground(objs, cx=True):
    """Centre on XY (optional) and drop to z=0."""
    mn, mx = bounds(objs); c = (mn + mx) / 2
    return xf(objs, (-c.x if cx else 0, -c.y if cx else 0, -mn.z))

def proc(o, slot):
    """Procedural helper part: box-projected UVs (metres) under the shared UV name, given a CC0 slot."""
    o.data.materials.clear(); o.data.materials.append(xmat(slot) if slot.startswith("X_") else mat(slot))
    bm = bmesh.new(); bm.from_mesh(o.data); uv = bm.loops.layers.uv.verify()
    for f_ in bm.faces:
        nx, ny, nz = (abs(c) for c in f_.normal)
        for l in f_.loops:
            co = o.matrix_world @ l.vert.co
            l[uv].uv = (co.y, co.z) if nx >= ny and nx >= nz else ((co.x, co.z) if ny >= nz else (co.x, co.y))
    bm.to_mesh(o.data); bm.free(); return o

def join(objs, name, decimate=None, smooth_deg=None):
    """Join keeping each part's UVs + materials; optional decimate ratio (LOD0 budget); origin stays at 0."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
        while len(o.data.uv_layers) > 1: o.data.uv_layers.remove(o.data.uv_layers[-1])
        if not o.data.uv_layers: o.data.uv_layers.new(name="UVMap")
        o.data.uv_layers[0].name = "UVMap"
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.make_single_user(object=True, obdata=True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(objs) > 1: bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active; o.name = name; o.data.name = name
    if decimate:
        md = o.modifiers.new("dec", "DECIMATE"); md.ratio = decimate; md.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier="dec")
    if smooth_deg is not None:
        bpy.ops.object.shade_auto_smooth(angle=math.radians(smooth_deg)) if hasattr(bpy.ops.object, "shade_auto_smooth") else None
    # Merge duplicate material slots (same datablock imported twice).
    seen = {}
    for i, ms in enumerate(o.material_slots): seen.setdefault(ms.material.name if ms.material else "", i)
    return o

def remap(o, m):
    """Rename classic slots of a procedural (omab_lib) mesh to CC0 slots, e.g. {'Dark': X_acg_Concrete034}."""
    for i, ms in enumerate(o.material_slots):
        k = ms.material.name.split(".")[0] if ms.material else ""
        if k in m: o.data.materials[i] = xmat(m[k]) if m[k].startswith("X_") else mat(m[k])
    return o

# ---------------------------------------------------------------- vehicles (OpenGameArt, CC0)
RGS = os.path.join(ROOT, "oga", "rgs", "Free Low Poly Vehicles Pack by Rgsdev")
def vehicle(src, name, police=False, length=None):
    """RGS low-poly car turned flyer: wheels -> glowing lift rings in the wheel arches, nose to +X,
    slots mapped to the game's livery/light slots."""
    before = set(bpy.data.objects); bpy.ops.import_scene.fbx(filepath=os.path.join(RGS, src, src + ".fbx"))
    new = [o for o in bpy.data.objects if o not in before]; bpy.context.view_layer.update()
    wheels = [o.matrix_world.translation.copy() for o in new if "wheel" in o.name]
    body = [o for o in new if o.type == "MESH" and "wheel" not in o.name]
    for o in new:
        if o not in body: bpy.data.objects.remove(o)
    for o in body: mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    m = {"body black": "Dark", "body white": "Paint", "body dark yellow": "Paint", "windows": "Glass", "headlights": "EmitWhite",
         "rear lights": "EmitAmber" if police else "EmitRed", "flashers siren red": "EmitRed", "flashers siren blue": "EmitBlue"}
    for o in body:
        for i, ms in enumerate(o.material_slots):
            k = ms.material.name.split(".")[0].lower() if ms.material else ""
            k = next((kk for kk in m if k.endswith(kk)), None)
            o.data.materials[i] = mat(m[k]) if k else mat("Paint" if "body" in (ms.material.name.lower() if ms.material else "") else "Dark")
    parts = list(body)
    for w in wheels:   # dark arch liner + lift ring where the wheel was
        parts.append(proc(cyl("arch", "Dark", .36, .26, loc=(w.x * .97, w.y, w.z + .05), rot=(0, math.pi / 2, 0), seg=20), "Dark"))
        parts.append(cyl("lift", "EmitBlue" if police else "EmitAmber", .3, .03, loc=(w.x * .9, w.y, .12), seg=20))
    xf(parts, rz=90)
    ground(parts); xf(parts, (0, 0, .15))
    if length:
        mn, mx = bounds(parts); xf(parts, s=length / (mx.x - mn.x))
    o = join(parts, name)
    for p in o.data.polygons: p.use_smooth = False
    return o

# ---------------------------------------------------------------- rooftop (T2)
def hvac():
    rust = acg("Metal041B"); plates = acg("MetalPlates006")
    parts = [proc(box("skid", "Dark", (3.5, 2.1, .16), loc=(0, 0, .08), bevel=.02), plates)]
    for i, (x, rot, which) in enumerate(((-.9, 0, None), (.9, 0, "rusted"), (-.9, 180, "rusted"), (.9, 180, None))):
        u = ph("exterior_aircon_unit", only=["exterior_aircon_unit_rusted"] if which else ["exterior_aircon_unit"], skip=() if which else ("rusted",))
        ground(u); mn, mx = bounds(u)
        xf(u, (0, -(mx.y - mn.y) / 2 - .02, 0)); xf(u, (x, 0, .16), rz=rot)
        parts += u
    d = ph("modular_airduct_rectangular_01", only=["modular_airduct_rectangular_01_bend_01"]); ground(d); xf(d, (1.9, .6, 0), rz=90)
    parts += d
    return join(parts, "SM_HVAC", decimate=.5)

def railing():
    f = ph("modular_chainlink_fence", only=["modular_chainlink_fence"], skip=("door", "corner", "bracket", "start", "end", "double", "post"))
    ground(f); mn, mx = bounds(f)
    if (mx.y - mn.y) > (mx.x - mn.x): xf(f, rz=90)
    mn, mx = bounds(f); xf(f, sv=(4.0 / (mx.x - mn.x), 1, 1.2 / (mx.z - mn.z)))
    ground(f)
    p = ph("modular_chainlink_fence", only=["modular_chainlink_fence_post"], skip=("bracket", "middle")); ground(p)
    mn, mx = bounds(p); xf(p, sv=(1, 1, 1.25 / (mx.z - mn.z))); xf(p, (-2.0, 0, 0))
    return join(f + p, "SM_Railing")

def antenna():
    rust = acg("Metal041B"); plates = acg("MetalPlates006")
    o = rooftop.antenna(); remap(o, {"Chrome": rust, "Paint": plates, "Dark": plates})
    parts = [o]
    cam = ph("security_camera_01"); ground(cam); xf(cam, (0, -.25, 6.0), rz=200); parts += cam
    for z, r in ((3.2, 0), (3.2, 180)):
        sl = ph("security_light"); ground(sl); xf(sl, (0, .2, z), rz=r); parts += sl
    return join(parts, "SM_Antenna", decimate=.5)

def tank():
    o = rooftop.tank(); remap(o, {"Paint": acg("PaintedMetal006"), "Dark": acg("Metal041B"), "Chrome": acg("Metal041B")})
    parts = [o]
    for i, (x, y) in enumerate(((2.6, -1.4), (2.9, -.7), (2.5, 1.3))):
        b = ph("barrel_03" if i < 2 else "propane_tank"); ground(b); xf(b, (x, y, 0), rz=i * 37, s=1.0 if i < 2 else 2.2); parts += b
    return join(parts, "SM_WaterTank")

def helipad():
    o = rooftop.helipad(); return remap(o, {"Dark": acg("Concrete034", .25), "Chrome": acg("MetalPlates006")})

# ---------------------------------------------------------------- market (T3)
def stall(seed):
    rng = random.Random(seed); w = rng.choice((2.4, 3.0, 3.6)); d = 1.6
    rust, corr = acg("Metal041B"), acg("CorrugatedSteel007A")
    parts = []
    sh = ph("rollershutter_window_01", only=["rollershutter_window_01_graffiti" if seed % 2 else "rollershutter_window_01"], skip=() if seed % 2 else ("graffiti",))
    ground(sh); mn, mx = bounds(sh)
    if (mx.y - mn.y) > (mx.x - mn.x): xf(sh, rz=90); mn, mx = bounds(sh)
    xf(sh, sv=(w / (mx.x - mn.x), 1, 2.5 / (mx.z - mn.z))); ground(sh); xf(sh, (0, d / 2, 0)); parts += sh
    cr = ph("wooden_crate_02"); ground(cr); mn, mx = bounds(cr); xf(cr, sv=(min(1.0, w / 2.6), 1.1, 1.6)); ground(cr); xf(cr, (0, -d / 2 + .35, 0)); parts += cr
    for sx in (-1, 1): parts.append(proc(cyl(f"post{sx}", "Chrome", .04, 2.6, loc=(sx * (w / 2 - .05), -d / 2 + .05, 1.3), seg=10), rust))
    parts.append(proc(box("awning", "Paint", (w + .4, d + .6, .03), loc=(0, -.1, 2.55), rot=(.18, 0, 0)), corr))
    parts.append(box("valance", "Paint", (w + .42, .03, .16), loc=(0, -d / 2 - .38, 2.42)))            # livery strip (tinted per stall)
    props = ["plastic_crate_01", "plastic_crate_03", "cardboard_box_01", "cardboard_box_01", "trashbag"]
    for i in range(rng.randint(3, 5)):
        p = ph(props[rng.randrange(len(props))]); ground(p)
        side = rng.choice((-1, 1)); xf(p, (side * rng.uniform(w / 2 - .5, w / 2 + .15), rng.uniform(-.1, .5), 0), rz=rng.uniform(0, 360)); parts += p
    if seed == 15:   # street "vending" cabinet next to the stall (CC0 utility cabinet)
        v = ph("utility_box_02"); ground(v); xf(v, (w / 2 + .55, .1, 0), rz=180); parts += v
    if seed == 8:
        t = ph("metal_trash_can", skip=("rust",)); ground(t); xf(t, (-w / 2 - .6, .2, 0), rz=90, s=.7); parts += t
    c = ph("modular_electric_cables", only=["cable_straight_long"]); ground(c); mn, mx = bounds(c)
    if (mx.y - mn.y) > (mx.x - mn.x): xf(c, rz=90); mn, mx = bounds(c)
    xf(c, sv=((w + .4) / (mx.x - mn.x), 1, 1)); ground(c); xf(c, (0, -d / 2 - .3, 2.3)); parts += c
    parts.append(cyl("lampw", "Dark", .01, .4, loc=(0, -.2, 2.25), seg=6))
    parts.append(cyl("lamp", "EmitAmber", .09, .12, loc=(0, -.2, 2.0), seg=14))
    parts += market.runes_face("pl", "EmitGlyph", 3, .28, rng, origin=(-.4, 2.15), y=-d / 2 - .02)
    for p in parts:
        if not p.data.uv_layers: proc(p, p.material_slots[0].material.name.split(".")[0])
    return join(parts, f"SM_Stall{seed}", decimate=.35)   # ~20-30k tris LOD0 (34 stalls line the street)

def vent():
    m = ph("water_manhole_cover"); ground(m); xf(m, s=1.4)
    return join(m, "SM_Vent")

def overpass():
    o = market.overpass(); return remap(o, {"Dark": acg("Concrete034", .25), "Paint": acg("Concrete034", .25)})

def world_surfaces():
    """Tiling CC0 surfaces the game puts on its procedural boxes (roof slab, parapets, pillars)."""
    for a in ("Concrete034", "Asphalt031", "MetalPlates006", "Metal041B", "PaintedMetal006", "CorrugatedSteel007A"): acg(a)

if __name__ == "__main__":
    for nm, fn in (("SM_Spinner", lambda: vehicle("Police Sports", "SM_Spinner", True, 5.6)), ("SM_CarA", lambda: vehicle("Sedan", "SM_CarA", False, 4.8)),
                   ("SM_CarB", lambda: vehicle("Van", "SM_CarB", False, 6.0)), ("SM_HVAC", hvac), ("SM_Railing", railing), ("SM_Antenna", antenna),
                   ("SM_WaterTank", tank), ("SM_Helipad", helipad), ("SM_Vent", vent), ("SM_Overpass", overpass)):
        reset(); o = fn(); export(o, nm)
    for sd in (3, 8, 15):
        reset(); export(stall(sd), f"SM_Stall{sd}")
    world_surfaces()
    json.dump(SLOTS, open(os.path.join(EXPORT, "cc0_slots.json"), "w"), indent=1, sort_keys=True)
    print("OMAB_CC0_DONE", len(SLOTS), "slots")

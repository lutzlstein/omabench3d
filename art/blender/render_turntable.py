"""Turntable preview of an exported FBX: blender -b --python render_turntable.py -- SM_Spinner [frames] [outdir]"""
import sys, os, math; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omab_lib import *
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
name = args[0] if args else "SM_Spinner"; frames = int(args[1]) if len(args) > 1 else 8
out = args[2] if len(args) > 2 else os.path.join(os.path.dirname(EXPORT), "renders")
reset(); sc = bpy.context.scene
bpy.ops.import_scene.fbx(filepath=os.path.join(EXPORT, name + ".fbx"))
o = [x for x in sc.objects if x.type == "MESH"][0]
for i, m in enumerate(o.data.materials):   # FBX import loses node setup: rebuild preview shaders from slot names
    o.data.materials[i] = mat(m.name.split(".")[0] + "")
bm = bmesh.new(); bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=20)
fl = obj_from_bm(bm, "floor", "Dark"); fl.data.materials[0].node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.05
for i, (c, loc, e) in enumerate([((1, .3, .7), (4, -4, 4), 400), ((.2, .7, 1), (-4, 4, 3), 400), ((1, .9, .8), (0, 0, 7), 250)]):
    ld = bpy.data.lights.new(f"L{i}", "AREA"); ld.energy = e; ld.color = c; ld.size = 3
    lo = bpy.data.objects.new(f"L{i}", ld); lo.location = loc; sc.collection.objects.link(lo)
    lo.rotation_euler = (Vector((0, 0, .6)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
w = bpy.data.worlds.new("w"); w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (.01, .01, .02, 1); sc.world = w
cd = bpy.data.cameras.new("cam"); cd.lens = 45; cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
sc.render.engine = "CYCLES"; sc.cycles.samples = 48; sc.cycles.use_denoising = True
try:
    prefs = bpy.context.preferences.addons["cycles"].preferences; prefs.compute_device_type = "OPTIX"; prefs.get_devices()
    for d in prefs.devices: d.use = True
    sc.cycles.device = "GPU"
except Exception as ex: print("cpu render", ex)
sc.render.resolution_x, sc.render.resolution_y = 960, 540
os.makedirs(out, exist_ok=True)
for f in range(frames):
    a = 2 * math.pi * f / frames + 0.6
    cam.location = (9 * math.cos(a), 9 * math.sin(a), 3.2)
    cam.rotation_euler = (Vector((0, 0, .6)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(out, f"{name}_{f:02d}.png"); bpy.ops.render.render(write_still=True)
print("OMAB_RENDER_OK", out)

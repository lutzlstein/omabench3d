"""Shared helpers for the OmaBench Blender asset generators (Blender 5.x, headless).
All geometry is procedural: no external meshes, textures or fonts. Units: metres, +X forward, +Z up."""
import bpy, bmesh, math, os, random
from mathutils import Vector, Matrix

EXPORT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "export")
SLOT_COLORS = {"Paint": (0.08, 0.1, 0.16, 1), "Chrome": (0.9, 0.9, 0.92, 1), "Glass": (0.2, 0.3, 0.4, 0.3),
               "Dark": (0.03, 0.03, 0.035, 1), "EmitWhite": (1, 0.95, 0.85, 1), "EmitRed": (1, 0.08, 0.05, 1),
               "EmitBlue": (0.1, 0.3, 1, 1), "EmitGlyph": (0, 0.9, 1, 1), "EmitAmber": (1, 0.6, 0.1, 1), "Marking": (0.9, 0.75, 0.1, 1)}

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def mat(name):
    m = bpy.data.materials.get(name)
    if m: return m
    m = bpy.data.materials.new(name); m.use_nodes = True
    c = SLOT_COLORS.get(name, (0.5, 0.5, 0.5, 1))
    b = m.node_tree.nodes.get("Principled BSDF")
    if b:
        b.inputs["Base Color"].default_value = c
        if name.startswith("Emit"):
            b.inputs["Emission Color"].default_value = c; b.inputs["Emission Strength"].default_value = 8.0
        if name == "Chrome": b.inputs["Metallic"].default_value = 1.0; b.inputs["Roughness"].default_value = 0.08
        if name == "Paint": b.inputs["Metallic"].default_value = 0.6; b.inputs["Roughness"].default_value = 0.25
        if name == "Glass": b.inputs["Transmission Weight"].default_value = 1.0; b.inputs["Roughness"].default_value = 0.02
    return m

def obj_from_bm(bm, name, slot, smooth=False):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(mat(slot))
    if smooth:
        for p in me.polygons: p.use_smooth = True
    return o

def loft(stations, n=24, exp=2.6, name="loft", slot="Paint", cap=True):
    """stations: list of (x, halfWidth, halfHeight, zCentre[, zTopBias]). Superellipse rings bridged into a tube."""
    bm = bmesh.new(); rings = []
    for st in stations:
        x, w, h, zc = st[:4]; flat = st[4] if len(st) > 4 else 0.0
        ring = []
        for i in range(n):
            a = 2 * math.pi * i / n
            ca, sa = math.cos(a), math.sin(a)
            y = w * math.copysign(abs(ca) ** (2 / exp), ca)
            z = h * math.copysign(abs(sa) ** (2 / exp), sa)
            if z < 0: z *= (1 - flat)          # flatter belly
            ring.append(bm.verts.new((x, y, zc + z)))
        rings.append(ring)
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(n):
            bm.faces.new((r0[i], r0[(i + 1) % n], r1[(i + 1) % n], r1[i]))
    if cap:
        bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, name, slot, smooth=True)

def box(name, slot, size, loc=(0, 0, 0), rot=(0, 0, 0), bevel=0.0):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=size, verts=bm.verts)
    o = obj_from_bm(bm, name, slot); o.location = loc; o.rotation_euler = rot
    if bevel > 0:
        m = o.modifiers.new("bev", "BEVEL"); m.width = bevel; m.segments = 2; m.limit_method = "ANGLE"
    return o

def cyl(name, slot, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), seg=24, bevel=0.0, r2=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r if r2 is None else r2, depth=depth)
    o = obj_from_bm(bm, name, slot, smooth=True); o.location = loc; o.rotation_euler = rot
    if bevel > 0:
        m = o.modifiers.new("bev", "BEVEL"); m.width = bevel; m.segments = 2; m.limit_method = "ANGLE"
    return o

def stroke(name, slot, p0, p1, width, depth, z=0.0):
    """Flat bar from p0 to p1 in the XZ plane (sign face), extruded along -Y by depth."""
    p0, p1 = Vector((p0[0], 0, p0[1])), Vector((p1[0], 0, p1[1]))
    d = p1 - p0; L = d.length
    o = box(name, slot, (L + width, depth, width), loc=tuple((p0 + p1) / 2 + Vector((0, 0, z))))
    o.rotation_euler = (0, -math.atan2(d.z, d.x), 0)
    return o

def subsurf(o, levels=2):
    m = o.modifiers.new("sub", "SUBSURF"); m.levels = levels; m.render_levels = levels
    return o

def bake(objs, name):
    """Apply modifiers, join into one mesh (material slots preserved), box-project UVs, origin at (0,0,0)."""
    dg = bpy.context.evaluated_depsgraph_get(); bm = bmesh.new(); mats = []
    for o in objs:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); me.transform(o.matrix_world)
        remap = []
        for m in me.materials:
            if m not in mats: mats.append(m)
            remap.append(mats.index(m))
        tmp = bmesh.new(); tmp.from_mesh(me)
        for f in tmp.faces: f.material_index = remap[f.material_index] if remap else 0
        me2 = bpy.data.meshes.new("t"); tmp.to_mesh(me2); tmp.free(); bm.from_mesh(me2)
    for o in objs: bpy.data.objects.remove(o)
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        nx, ny, nz = (abs(c) for c in f.normal)
        for l in f.loops:
            co = l.vert.co
            l[uv].uv = (co.y, co.z) if nx >= ny and nx >= nz else ((co.x, co.z) if ny >= nz else (co.x, co.y))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    return o

def export(o, name):
    os.makedirs(EXPORT, exist_ok=True)
    for x in bpy.context.scene.objects: x.select_set(False)
    o.select_set(True); bpy.context.view_layer.objects.active = o
    path = os.path.join(EXPORT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
                             axis_forward="X", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True, bake_space_transform=True,
                             add_leaf_bones=False, path_mode="STRIP", embed_textures=False)
    print("OMAB_EXPORT", name, len(o.data.polygons), "faces", [m.name for m in o.data.materials])
    return path

# Invented runic glyphs: a stave plus 1-3 branch strokes on a 3x5 lattice (Futhark-like, not any real script).
_BRANCHES = [((1, 4), (2, 3)), ((1, 4), (0, 3)), ((1, 3), (2, 2)), ((1, 3), (0, 2)), ((1, 2), (2, 1)), ((1, 2), (0, 1)),
             ((1, 2), (2, 3)), ((1, 1), (2, 0)), ((1, 1), (0, 0)), ((0, 2), (2, 2)), ((2, 4), (2, 2)), ((0, 4), (2, 0)),
             ((1, 4), (2, 4)), ((1, 0), (0, 0))]
def glyph_strokes(rng):
    staves = [((1, 0), (1, 4))] if rng.random() < 0.75 else [((0, 0), (0, 4)), ((2, 0), (2, 4))]
    k = rng.randint(1, 3)
    return staves + rng.sample(_BRANCHES, k)

def rune_line(prefix, slot, n, cell, rng, vertical=True, width=0.06, depth=0.04, origin=(0, 0)):
    objs = []
    for g in range(n):
        ox, oz = (origin[0], origin[1] - g * cell * 1.5) if vertical else (origin[0] + g * cell * 0.9, origin[1])
        for s, (a, b) in enumerate(glyph_strokes(rng)):
            sc = cell / 4
            objs.append(stroke(f"{prefix}{g}_{s}", slot, (ox + a[0] * sc, oz + a[1] * sc), (ox + b[0] * sc, oz + b[1] * sc), width, depth))
    return objs

# CC0 textures -> UE: imports the texture sets listed in art/export/cc0_slots.json (written by
# art/blender/cc0_kit.py), builds two masters (mesh-UV and world-projected), one instance per slot, assigns
# them to every X_* material slot of the imported meshes, and adds LODs to all OmaBench meshes.
import unreal, os, json
L = unreal.MaterialEditingLibrary; EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools(); MP = unreal.MaterialProperty
proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SLOTS = json.load(open(os.path.join(proj, "art", "export", "cc0_slots.json")))
TEX, MAT, MESH = "/Game/OmaBench/CC0/Tex", "/Game/OmaBench/CC0", "/Game/OmaBench/Meshes"

# ---- textures
files = sorted({v[k] for v in SLOTS.values() for k in ("base", "arm", "nor", "mask") if v.get(k)})
tasks = []
for f in files:
    t = unreal.AssetImportTask(); t.filename = f; t.destination_path = TEX; t.automated = True; t.replace_existing = True; t.save = False; tasks.append(t)
AT.import_asset_tasks(tasks)
def tex(f):
    if not f: return None
    n = os.path.splitext(os.path.basename(f))[0]
    n = "".join(c if c.isalnum() or c == "_" else "_" for c in n)
    return EAL.load_asset(f"{TEX}/{n}")
for v in SLOTS.values():
    for k in ("arm", "mask", "nor"):
        t = tex(v.get(k))
        if not t: continue
        t.set_editor_property("srgb", False)
        if k == "nor":
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP); t.set_editor_property("flip_green_channel", True)
        elif k == "mask":
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        else:
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
for f in files:
    t = tex(f)
    if t: t.set_editor_property("max_texture_size", 1024); EAL.save_loaded_asset(t)

# ---- masters
def fresh(name):
    p = f"{MAT}/{name}"
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    return AT.create_asset(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
def ex(m, cls, x, y): return L.create_material_expression(m, cls, x, y)
def custom(m, code, inputs, x, y, out):
    e = ex(m, unreal.MaterialExpressionCustom, x, y); e.set_editor_property("code", code); e.set_editor_property("output_type", out)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
    e.set_editor_property("inputs", ins); return e
def scalar(m, n, d, x, y):
    e = ex(m, unreal.MaterialExpressionScalarParameter, x, y); e.set_editor_property("parameter_name", n); e.set_editor_property("default_value", d); return e
F2, F3, F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT2, unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT1
DEF = {"Base": "/Engine/EngineResources/DefaultTexture", "ARM": "/Engine/EngineResources/WhiteSquareTexture",
       "Normal": "/Engine/EngineMaterials/DefaultNormal", "Mask": "/Engine/EngineResources/WhiteSquareTexture"}
def master(name, world=False, masked=False):
    m = fresh(name)
    if masked:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED); m.set_editor_property("two_sided", True)
    if world:
        wp = ex(m, unreal.MaterialExpressionWorldPosition, -1600, 0); vn = ex(m, unreal.MaterialExpressionVertexNormalWS, -1600, 150)
        uv = custom(m, "float3 a = abs(N); float2 c = a.z > 0.5 ? P.xy : (a.x > a.y ? P.yz : P.xz); return c / (100.0 / max(T, 0.001));", ["P", "N", "T"], -1350, 50, F2)
        L.connect_material_expressions(wp, "", uv, "P"); L.connect_material_expressions(vn, "", uv, "N")
        L.connect_material_expressions(scalar(m, "Tile", 1.0, -1600, 300), "", uv, "T")
    else:
        tc = ex(m, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
        uv = custom(m, "return UV * T;", ["UV", "T"], -1350, 50, F2)
        L.connect_material_expressions(tc, "", uv, "UV"); L.connect_material_expressions(scalar(m, "Tile", 1.0, -1600, 300), "", uv, "T")
    S = {}
    for i, (n, st) in enumerate((("Base", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR), ("ARM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),
                                 ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL), ("Mask", unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE))):
        if n == "Mask" and not masked: continue
        e = ex(m, unreal.MaterialExpressionTextureSampleParameter2D, -1000, i * 260); e.set_editor_property("parameter_name", n)
        e.set_editor_property("texture", unreal.load_asset(DEF[n])); e.set_editor_property("sampler_type", st)
        L.connect_material_expressions(uv, "", e, "UVs"); S[n] = e
    tint = ex(m, unreal.MaterialExpressionVectorParameter, -1000, -260); tint.set_editor_property("parameter_name", "Tint"); tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    wet = scalar(m, "Wet", 0.0, -1000, 1100); rm = scalar(m, "RoughMul", 1.0, -1000, 1200)
    # Wet look: darker albedo, much lower roughness on up-facing surfaces (puddled), a little on walls.
    vn2 = ex(m, unreal.MaterialExpressionVertexNormalWS, -1000, 1300)
    bc = custom(m, "float w = W * lerp(0.5, 1.0, saturate(N.z)); return B.rgb * T.rgb * lerp(1.0, 0.55, w);", ["B", "T", "W", "N"], -500, -100, F3)
    L.connect_material_expressions(S["Base"], "RGB", bc, "B"); L.connect_material_expressions(tint, "", bc, "T"); L.connect_material_expressions(wet, "", bc, "W"); L.connect_material_expressions(vn2, "", bc, "N")
    ro = custom(m, "float w = W * lerp(0.4, 1.0, saturate(N.z)); return saturate(A.g * R * lerp(1.0, 0.18, w));", ["A", "R", "W", "N"], -500, 200, F1)
    L.connect_material_expressions(S["ARM"], "RGB", ro, "A"); L.connect_material_expressions(rm, "", ro, "R"); L.connect_material_expressions(wet, "", ro, "W"); L.connect_material_expressions(vn2, "", ro, "N")
    L.connect_material_property(bc, "", MP.MP_BASE_COLOR); L.connect_material_property(ro, "", MP.MP_ROUGHNESS)
    L.connect_material_property(S["ARM"], "B", MP.MP_METALLIC); L.connect_material_property(S["ARM"], "R", MP.MP_AMBIENT_OCCLUSION)
    L.connect_material_property(S["Normal"], "RGB", MP.MP_NORMAL)
    if masked: L.connect_material_property(S["Mask"], "R", MP.MP_OPACITY_MASK)
    L.recompile_material(m); EAL.save_loaded_asset(m); return m
M_MESH, M_MASK, M_WORLD = master("M_CC0"), master("M_CC0Masked", masked=True), master("M_CC0World", world=True)

# ---- instances (mesh slots + world-projected variants for the game's procedural boxes)
def mic(name, parent, v, tile=1.0, wet=0.0):
    p = f"{MAT}/{name}"
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    mi = AT.create_asset(name, MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    L.set_material_instance_parent(mi, parent)
    for k, n in (("base", "Base"), ("arm", "ARM"), ("nor", "Normal"), ("mask", "Mask")):
        t = tex(v.get(k))
        if t: L.set_material_instance_texture_parameter_value(mi, n, t)
    L.set_material_instance_scalar_parameter_value(mi, "Tile", tile); L.set_material_instance_scalar_parameter_value(mi, "Wet", wet)
    EAL.save_loaded_asset(mi); return mi
INST = {}
for s, v in SLOTS.items():
    INST[s] = mic("MI_" + s[2:], M_MASK if v.get("mask") else M_MESH, v, v.get("tile", 1.0) if s.startswith("X_acg_") else 1.0,
                  0.6 if s.startswith("X_acg_") else 0.25)
for a, tile, wet in (("Concrete034", .35, 1.0), ("Asphalt031", .3, 1.0), ("MetalPlates006", .5, .7), ("Metal041B", .5, .5), ("PaintedMetal006", .5, .6), ("CorrugatedSteel007A", .5, .6)):
    v = SLOTS.get("X_acg_" + a)
    if v: mic("MIW_" + a, M_WORLD, v, tile, wet)

# ---- assign to meshes + LODs
HEAVY = ("SM_HVAC", "SM_Antenna", "SM_WaterTank", "SM_Stall", "SM_Vent", "SM_Railing", "SM_Billboard", "SM_Sign")
n_assigned = 0
for p in EAL.list_assets(MESH, recursive=False):
    sm = EAL.load_asset(p)
    if not isinstance(sm, unreal.StaticMesh): continue
    mats = sm.get_editor_property("static_materials"); changed = False
    for i, sm_m in enumerate(mats):
        n = str(sm_m.get_editor_property("material_slot_name")).split(".")[0]
        if n in INST:
            sm_m.set_editor_property("material_interface", INST[n]); changed = True; n_assigned += 1
    if changed: sm.set_editor_property("static_materials", mats)
    # LODs: engine "LargeProp" LOD group = 4 LODs, 50% tris per step, auto screen sizes (mesh reduction on build).
    # (StaticMeshEditorSubsystem is unavailable under -run=pythonscript; the LOD group path works headless.)
    if any(k in p for k in HEAVY):
        sm.set_editor_property("lod_group", "LargeProp")
    EAL.save_loaded_asset(sm)
    unreal.log("OMABENCH_CC0 %s lods=%d" % (p, sm.get_num_lods()))
unreal.log("OMABENCH_CC0_DONE slots=%d assigned=%d" % (len(INST), n_assigned))

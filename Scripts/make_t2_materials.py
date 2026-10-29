# Builds OmaBench neon materials headlessly. Run via UnrealEditor-Cmd -run=pythonscript.
import unreal
L = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/OmaBench/Materials"

def fresh(name):
    path = f"{DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return AT.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())

def vparam(m, name, val, x, y):
    e = L.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name); e.set_editor_property("default_value", unreal.LinearColor(*val)); return e
def sparam(m, name, val, x, y):
    e = L.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name); e.set_editor_property("default_value", val); return e
def custom(m, code, inputs, x, y):
    e = L.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
    e.set_editor_property("inputs", ins); return e


def scalar_custom(m, code, inputs, x, y):
    e = custom(m, code, inputs, x, y); e.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1); return e
def setp(m, k, v):
    try: m.set_editor_property(k, v)
    except Exception as e: unreal.log_warning(f"{m.get_name()} {k}: {e}")

# ---- T2 Off-World Hangar materials ------------------------------------------------------------
# M_Chrome: polished metal for spinner bodies/rails; faint world-space panel seams raise roughness on lines.
m = fresh("M_Chrome")
wp = L.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 0)
pr = sparam(m, "Roughness", 0.06, -900, 150)
r = scalar_custom(m, "float2 g = abs(frac(P.xy / 180.0) - 0.5); float seam = step(0.485, max(g.x, g.y)); return lerp(R, 0.45, seam);", ["P","R"], -500, 0)
L.connect_material_expressions(wp, "", r, "P"); L.connect_material_expressions(pr, "", r, "R")
L.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
L.connect_material_property(vparam(m,"Base",(0.85,0.86,0.9,1),-500,200), "", unreal.MaterialProperty.MP_BASE_COLOR)
L.connect_material_property(sparam(m,"Metallic",1.0,-500,300), "", unreal.MaterialProperty.MP_METALLIC)
setp(m, "used_with_instanced_static_meshes", True)
L.recompile_material(m)

# M_Floor: mirror-polished dark floor with emissive inlay grid (world-space).
m = fresh("M_Floor")
wp = L.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 0)
g = scalar_custom(m, "float2 g = abs(frac(P.xy / 400.0) - 0.5); return step(0.492, max(g.x, g.y));", ["P"], -500, 0)
L.connect_material_expressions(wp, "", g, "P")
lc = vparam(m, "LineColor", (0.0, 0.6, 1.0, 1), -900, 200)
em = custom(m, "return C.rgb * G * 2.0;", ["C","G"], -300, 100)
L.connect_material_expressions(lc, "", em, "C"); L.connect_material_expressions(g, "", em, "G")
L.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
rr = scalar_custom(m, "return lerp(0.03, 0.3, G);", ["G"], -300, 250); L.connect_material_expressions(g, "", rr, "G")
L.connect_material_property(rr, "", unreal.MaterialProperty.MP_ROUGHNESS)
L.connect_material_property(vparam(m,"Base",(0.02,0.02,0.025,1),-500,350), "", unreal.MaterialProperty.MP_BASE_COLOR)
L.connect_material_property(sparam(m,"Metallic",0.85,-500,450), "", unreal.MaterialProperty.MP_METALLIC)
setp(m, "used_with_instanced_static_meshes", True)
L.recompile_material(m)

# M_Glass: clear translucent glass, forward-shaded surface so Lumen/RT translucency reflections apply.
m = fresh("M_Glass")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
setp(m, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
setp(m, "two_sided", True)
setp(m, "used_with_instanced_static_meshes", True)
L.connect_material_property(vparam(m,"Tint",(0.55,0.75,0.9,1),-500,0), "", unreal.MaterialProperty.MP_BASE_COLOR)
L.connect_material_property(sparam(m,"Opacity",0.18,-500,100), "", unreal.MaterialProperty.MP_OPACITY)
L.connect_material_property(sparam(m,"Roughness",0.02,-500,200), "", unreal.MaterialProperty.MP_ROUGHNESS)
L.connect_material_property(sparam(m,"Metallic",0.0,-500,300), "", unreal.MaterialProperty.MP_METALLIC)
L.connect_material_property(sparam(m,"Specular",1.0,-500,400), "", unreal.MaterialProperty.MP_SPECULAR)
L.recompile_material(m)

# M_Emit: opaque unlit emissive (light strips, running lights, planet, stars).
m = fresh("M_Emit")
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
setp(m, "used_with_instanced_static_meshes", True)
L.connect_material_property(vparam(m,"Color",(1,1,1,1),-500,0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.recompile_material(m)

# M_Holo: additive hologram with scanlines and a deterministic Time-driven flicker.
m = fresh("M_Holo")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
setp(m, "two_sided", True)
setp(m, "used_with_instanced_static_meshes", True)
wp = L.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 0)
t = L.create_material_expression(m, unreal.MaterialExpressionTime, -900, 100)
c = vparam(m, "Color", (0.0, 0.9, 1.0, 1), -900, 200)
h = custom(m, "float s = 0.55 + 0.45 * step(0.5, frac(P.z / 12.0 - T * 0.8)); float f = 0.85 + 0.15 * sin(T * 23.0) * sin(T * 7.0); return C.rgb * s * f;", ["P","T","C"], -500, 0)
for src, pin in ((wp,"P"),(t,"T"),(c,"C")): L.connect_material_expressions(src, "", h, pin)
L.connect_material_property(h, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.recompile_material(m)

unreal.EditorAssetLibrary.save_directory(DIR)
unreal.log("OMABENCH_T2_MATERIALS_OK")

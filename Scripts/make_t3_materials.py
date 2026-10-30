# T3 "Downpour" particle materials (Niagara sprites/meshes). Run via UnrealEditor-Cmd -run=pythonscript.
import unreal
L = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/OmaBench/Materials"
MP = unreal.MaterialProperty

def fresh(name):
    p = f"{DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(p): unreal.EditorAssetLibrary.delete_asset(p)
    return AT.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
def ex(m, cls, x, y): return L.create_material_expression(m, cls, x, y)
def custom(m, code, inputs, x, y, out=unreal.CustomMaterialOutputType.CMOT_FLOAT1):
    e = ex(m, unreal.MaterialExpressionCustom, x, y); e.set_editor_property("code", code); e.set_editor_property("output_type", out)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
    e.set_editor_property("inputs", ins); return e
def base(name, blend, unlit=True):
    m = fresh(name)
    m.set_editor_property("blend_mode", blend)
    if unlit: m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    for f in ("used_with_niagara_sprites", "used_with_niagara_mesh_particles", "used_with_niagara_ribbons"): m.set_editor_property(f, True)
    return m

# Soft streak/drop: radial falloff of the sprite quad * particle colour (velocity-aligned sprites stretch it into streaks).
def soft_sprite(name, blend, gain, depth_fade=0.0):
    m = base(name, blend)
    pc = ex(m, unreal.MaterialExpressionParticleColor, -900, 0)
    uv = ex(m, unreal.MaterialExpressionTextureCoordinate, -900, 200)
    sh = custom(m, "float2 d = UV * 2 - 1; return saturate(1 - dot(d, d));", ["UV"], -600, 200)
    L.connect_material_expressions(uv, "", sh, "UV")
    em = custom(m, f"return C.rgb * {gain} * (S * A);", ["C", "S", "A"], -300, 0, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    L.connect_material_expressions(pc, "", em, "C"); L.connect_material_expressions(sh, "", em, "S"); L.connect_material_expressions(pc, "A", em, "A")
    L.connect_material_property(em, "", MP.MP_EMISSIVE_COLOR)
    if blend == unreal.BlendMode.BLEND_TRANSLUCENT:
        op = custom(m, "return S * A;", ["A", "S"], -300, 300)
        L.connect_material_expressions(pc, "A", op, "A"); L.connect_material_expressions(sh, "", op, "S")
        L.connect_material_property(op, "", MP.MP_OPACITY)
    L.recompile_material(m)

soft_sprite("M_FxRain", unreal.BlendMode.BLEND_ADDITIVE, 1.0)
soft_sprite("M_FxSpark", unreal.BlendMode.BLEND_ADDITIVE, 1.0)

# Smoke/steam: lit translucent puffs, lit per vertex (cheap; the puffs are soft anyway), 2-octave ALU noise. Per-pixel 3D noise (animated, world space) + depth fade -> real fill-rate/overdraw cost.
m = base("M_FxSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, unlit=False)
m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL)
pc = ex(m, unreal.MaterialExpressionParticleColor, -1100, 0)
uv = ex(m, unreal.MaterialExpressionTextureCoordinate, -1100, 200)
wp = ex(m, unreal.MaterialExpressionWorldPosition, -1100, 350)
tm = ex(m, unreal.MaterialExpressionTime, -1100, 500)
nz = ex(m, unreal.MaterialExpressionNoise, -800, 350)
nz.set_editor_property("scale", 0.004); nz.set_editor_property("levels", 2); nz.set_editor_property("output_min", 0.0); nz.set_editor_property("output_max", 1.0)
nz.set_editor_property("noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU)
pos = custom(m, "return P + float3(0, 0, -T * 40);", ["P", "T"], -950, 420, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
L.connect_material_expressions(wp, "", pos, "P"); L.connect_material_expressions(tm, "", pos, "T")
L.connect_material_expressions(pos, "", nz, "Position")
df = ex(m, unreal.MaterialExpressionDepthFade, -600, 550); df.set_editor_property("fade_distance_default", 120.0)
op = custom(m, "float2 d = UV * 2 - 1; float r = saturate(1 - dot(d, d)); return saturate(r * r * (0.35 + N) * A * F);", ["UV", "N", "A", "F"], -400, 250)
L.connect_material_expressions(uv, "", op, "UV"); L.connect_material_expressions(nz, "", op, "N"); L.connect_material_expressions(pc, "A", op, "A"); L.connect_material_expressions(df, "", op, "F")
L.connect_material_property(op, "", MP.MP_OPACITY)
L.connect_material_property(pc, "", MP.MP_BASE_COLOR)
L.connect_material_property(custom(m, "return 0.95;", [], -400, 600), "", MP.MP_ROUGHNESS)
L.recompile_material(m)

for n in ("M_FxRain", "M_FxSpark", "M_FxSmoke"): unreal.EditorAssetLibrary.save_asset(f"{DIR}/{n}")
unreal.log("OMABENCH_T3_MATERIALS_DONE")

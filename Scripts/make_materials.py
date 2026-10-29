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

# M_Tower: dark reflective glass/metal body with procedural lit windows (world-space grid, per-instance random).
m = fresh("M_Tower")
wp = L.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1000, 0)
nrm = L.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -1000, 100)
rnd = L.create_material_expression(m, unreal.MaterialExpressionPerInstanceRandom, -1000, 200)
win = vparam(m, "WindowColor", (1.0,0.75,0.45,1), -1000, 300)
g = sparam(m, "Glow", 6.0, -1000, 400)
dens = sparam(m, "LitFraction", 0.45, -1000, 500)
code = r"""
float u = (abs(N.x) > 0.5 ? WP.y : WP.x) / 160.0; float v = WP.z / 220.0;
float2 f = frac(float2(u, v)); float2 id = floor(float2(u, v));
float w = step(0.18, f.x) * step(f.x, 0.82) * step(0.28, f.y) * step(f.y, 0.78);
float h = frac(sin(dot(id + R * 137.0, float2(12.9898, 78.233))) * 43758.5453);
float lit = step(1.0 - Lit, h) * step(abs(N.z), 0.5);
float3 col = lerp(Win, float3(Win.b, Win.g * 0.9, Win.r) * 1.2, step(0.9, h));
return col * Glow * w * lit * (0.5 + frac(h * 7.0));
"""
c = custom(m, code, ["WP","N","R","Win","Glow","Lit"], -500, 0)
for src, pin in ((wp,"WP"),(nrm,"N"),(rnd,"R"),(win,"Win"),(g,"Glow"),(dens,"Lit")): L.connect_material_expressions(src, "", c, pin)
L.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.connect_material_property(vparam(m,"BodyColor",(0.012,0.012,0.018,1),-500,300), "", unreal.MaterialProperty.MP_BASE_COLOR)
L.connect_material_property(sparam(m,"Metallic",0.7,-500,400), "", unreal.MaterialProperty.MP_METALLIC)
L.connect_material_property(sparam(m,"Roughness",0.1,-500,500), "", unreal.MaterialProperty.MP_ROUGHNESS)
m.set_editor_property("used_with_instanced_static_meshes", True)
L.recompile_material(m)

# M_Sign: emissive neon billboard panel with horizontal banding.
m = fresh("M_Sign")
uv = L.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -800, 0)
col = vparam(m, "Color", (1.0,0.2,0.6,1), -800, 100)
g = sparam(m, "Glow", 25.0, -800, 200)
c = custom(m, "float s = 0.65 + 0.35 * step(0.35, frac(UV.y * 6.0)); float e = 1.0 + 1.5 * step(min(min(UV.x, 1.0 - UV.x), min(UV.y, 1.0 - UV.y)), 0.06); return Col * Glow * s * e;", ["UV","Col","Glow"], -400, 0)
for src, pin in ((uv,"UV"),(col,"Col"),(g,"Glow")): L.connect_material_expressions(src, "", c, pin)
L.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.connect_material_property(vparam(m,"Base",(0.02,0.02,0.02,1),-400,200), "", unreal.MaterialProperty.MP_BASE_COLOR)
m.set_editor_property("used_with_instanced_static_meshes", True)
L.recompile_material(m)

# M_Street: wet asphalt, puddle roughness from a world-space value-noise, faint lane lines.
m = fresh("M_Street")
wp = L.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -800, 0)
code = r"""
float2 p = WP.xy / 700.0; float2 i = floor(p); float2 f = frac(p); f = f * f * (3.0 - 2.0 * f);
float a = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5);
float b = frac(sin(dot(i + float2(1,0), float2(127.1, 311.7))) * 43758.5);
float c = frac(sin(dot(i + float2(0,1), float2(127.1, 311.7))) * 43758.5);
float d = frac(sin(dot(i + float2(1,1), float2(127.1, 311.7))) * 43758.5);
float n = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
return lerp(0.22, 0.015, smoothstep(0.25, 0.5, n));
"""
r = custom(m, code, ["WP"], -400, 0)
r.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
L.connect_material_expressions(wp, "", r, "WP")
L.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
lane = custom(m, "float2 q = abs(frac(WP.xy / 900.0) - 0.5); float k = step(0.494, max(q.x, q.y)) * step(0.5, frac(WP.x / 300.0 + WP.y / 300.0)); return float3(1.0, 0.7, 0.2) * k * 0.6;", ["WP"], -400, 200)
L.connect_material_expressions(wp, "", lane, "WP")
L.connect_material_property(lane, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.connect_material_property(vparam(m,"Base",(0.015,0.015,0.018,1),-400,400), "", unreal.MaterialProperty.MP_BASE_COLOR)
m.set_editor_property("used_with_instanced_static_meshes", True)
L.recompile_material(m)

# M_Rain: additive unlit streaks; vertical motion is a world-position offset driven by material Time,
# wrapped inside a box that follows the camera, so it costs a single instanced draw and is fully deterministic.
m = fresh("M_Rain")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
# Additive streaks write no velocity, so TSR history rejected them whenever the camera moved
# (rain only showed while the camera was slow at start/end). Render after motion blur/TSR instead.
for k, v in (("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR), ("enable_responsive_aa", True),
             # Per-instance GPU culling uses the undisplaced streak bounds (a few cm). The WPO drops
             # streaks up to BoxHeight, so without this most of them were culled as the camera moved.
             ("max_world_position_offset_displacement", 4200.0)):
    try: m.set_editor_property(k, v)
    except Exception as e: unreal.log_warning(f"M_Rain {k}: {e}")
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("used_with_instanced_static_meshes", True)
t = L.create_material_expression(m, unreal.MaterialExpressionTime, -900, 0)
rnd = L.create_material_expression(m, unreal.MaterialExpressionPerInstanceRandom, -900, 100)
sp = sparam(m, "Speed", 900.0, -900, 200)
hh = sparam(m, "BoxHeight", 4000.0, -900, 300)
wpo = custom(m, "float d = frac((T * S) / H + R) * H; return float3(-0.06 * d, 0.03 * d, -d);", ["T","R","S","H"], -500, 0)
for src, pin in ((t,"T"),(rnd,"R"),(sp,"S"),(hh,"H")): L.connect_material_expressions(src, "", wpo, pin)
L.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
L.connect_material_property(vparam(m,"Color",(0.35,0.4,0.55,1),-500,200), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.recompile_material(m)

# PP_CRT: post-only pixelation (point-sampled virtual low-res grid), ordered-dither colour quantisation,
# per-block scanlines and phosphor tint. Runs after tonemapping on the full-res image, so GPU render load is unchanged.
m = fresh("PP_CRT")
m.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
m.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
sp = L.create_material_expression(m, unreal.MaterialExpressionScreenPosition, -1200, 0)
vs = L.create_material_expression(m, unreal.MaterialExpressionViewSize, -1200, 150)
px = sparam(m, "PixelSize", 4.0, -1200, 250)
q = custom(m, "float p = max(P, 1.0); float2 cell = floor(UV * VS / p); return float3((cell + 0.5) * p / VS, 0);", ["UV","VS","P"], -900, 0)
q.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
L.connect_material_expressions(sp, "ViewportUV", q, "UV"); L.connect_material_expressions(vs, "", q, "VS"); L.connect_material_expressions(px, "", q, "P")
q.set_editor_property("code", "float p = max(P, 1.0); float2 cell = floor(UV * VS / p); return (cell + 0.5) * p / VS;")
st = L.create_material_expression(m, unreal.MaterialExpressionSceneTexture, -600, 0)
st.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
st.set_editor_property("filtered", False)
L.connect_material_expressions(q, "", st, "UVs")
s_ = sparam(m, "Scanline", 0.25, -600, 200)
lv = sparam(m, "Levels", 12.0, -600, 300)
tint = vparam(m, "Tint", (1.0,0.95,0.9,1), -600, 400)
code = r"""
float p = max(P, 1.0);
float2 pix = floor(Parameters.SvPosition.xy / p);
float3 c = Scene.rgb * Tint;
if (Levels > 1.0) {
  int2 b = int2(pix) & 3;
  const float bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
  float d = (bayer[b.y*4+b.x] + 0.5) / 16.0 - 0.5;
  c = floor(saturate(c) * (Levels - 1.0) + 0.5 + d) / (Levels - 1.0);
}
float row = frac(Parameters.SvPosition.y / p);
float l = 1.0 - Strength * smoothstep(0.55, 1.0, row);
return c * l;
"""
c = custom(m, code, ["Scene","Strength","Tint","Levels","P"], -300, 0)
L.connect_material_expressions(st, "Color", c, "Scene"); L.connect_material_expressions(s_, "", c, "Strength")
L.connect_material_expressions(tint, "", c, "Tint"); L.connect_material_expressions(lv, "", c, "Levels"); L.connect_material_expressions(px, "", c, "P")
L.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
L.recompile_material(m)

unreal.EditorAssetLibrary.save_directory(DIR)
unreal.log("OMABENCH_MATERIALS_OK")

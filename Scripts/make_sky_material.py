# M_NightSky: cheap procedural sky dome for every test (one unlit pass, no textures, no volumetrics).
# Dark overcast zenith, city light-pollution glow on the horizon (warm <-> magenta/cyan by azimuth) and a
# low, scrolling rain-cloud deck lit from below by that glow. Octaves param scales cost per tier.
import unreal
L = unreal.MaterialEditingLibrary; EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools(); MP = unreal.MaterialProperty
DIR, NAME = "/Game/OmaBench/Materials", "M_NightSky"
if EAL.does_asset_exist(f"{DIR}/{NAME}"): EAL.delete_asset(f"{DIR}/{NAME}")
m = AT.create_asset(NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("two_sided", True); m.set_editor_property("is_sky", True)
def ex(cls, x, y): return L.create_material_expression(m, cls, x, y)
def sp(n, d, y):
    e = ex(unreal.MaterialExpressionScalarParameter, -900, y); e.set_editor_property("parameter_name", n); e.set_editor_property("default_value", d); return e
def vp(n, c, y):
    e = ex(unreal.MaterialExpressionVectorParameter, -900, y); e.set_editor_property("parameter_name", n); e.set_editor_property("default_value", unreal.LinearColor(*c, 1)); return e
CODE = r"""
float3 d = normalize(P - C);
float h = d.z;
float az = atan2(d.y, d.x);
// horizon glow: warm sodium city light, drifting to magenta / cyan neon by azimuth
float m1 = 0.5 + 0.5 * sin(az * 2.0 + 0.7), m2 = 0.5 + 0.5 * sin(az * 3.0 - 1.9);
float3 glow = lerp(Warm, lerp(Mag, Cya, m2), 0.55 * m1);
float hz = exp(-max(h, 0.0) * 5.0);                  // glow falloff above the horizon
float3 sky = Zen + glow * hz * Glow;
// cloud deck: project onto a plane, fbm value noise, scrolled by wind
float2 uv = d.xy / (max(h, 0.0) + 0.06) * Scale + Tm * Wind;
float n = 0.0, a = 0.5, nrm = 0.0;
for (int i = 0; i < (int)Oct; i++) {
  float2 ip = floor(uv), fp = frac(uv); fp = fp * fp * (3.0 - 2.0 * fp);
  float2 o = float2(1.0, 0.0);
  float h00 = frac(sin(dot(ip,          float2(127.1, 311.7))) * 43758.5453);
  float h10 = frac(sin(dot(ip + o.xy,   float2(127.1, 311.7))) * 43758.5453);
  float h01 = frac(sin(dot(ip + o.yx,   float2(127.1, 311.7))) * 43758.5453);
  float h11 = frac(sin(dot(ip + 1.0,    float2(127.1, 311.7))) * 43758.5453);
  n += a * lerp(lerp(h00, h10, fp.x), lerp(h01, h11, fp.x), fp.y); nrm += a;
  uv = mul(float2x2(1.6, 1.2, -1.2, 1.6), uv); a *= 0.5;
}
n /= max(nrm, 0.001);
float c = smoothstep(Cover, Cover + 0.3, n) * saturate(h * 10.0);
// underlit: thick cloud bellies catch the city glow, strongest toward the horizon
float under = 0.35 + 0.65 * hz;
float3 cloud = Zen * 1.4 + glow * Glow * (0.25 + 0.75 * n) * under * CloudLit;
sky = lerp(sky, cloud, c);
// below the horizon: the glow fades into the haze colour
sky = h < 0.0 ? (Zen + glow * Glow) * saturate(1.0 + h * 3.0) : sky;
return sky * Intensity;
"""
cu = ex(unreal.MaterialExpressionCustom, -400, 0)
cu.set_editor_property("code", CODE); cu.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
names = ["P", "C", "Tm", "Zen", "Warm", "Mag", "Cya", "Glow", "Scale", "Wind", "Oct", "Cover", "CloudLit", "Intensity"]
ins = []
for n in names:
    ci = unreal.CustomInput(); ci.set_editor_property("input_name", n); ins.append(ci)
cu.set_editor_property("inputs", ins)
wp = ex(unreal.MaterialExpressionWorldPosition, -900, -400)
cp = ex(unreal.MaterialExpressionCameraPositionWS, -900, -300)
tm = ex(unreal.MaterialExpressionTime, -900, -200)
srcs = {"P": wp, "C": cp, "Tm": tm,
        "Zen": vp("Zenith", (0.006, 0.006, 0.011), 0), "Warm": vp("GlowWarm", (1.0, 0.45, 0.18), 100),
        "Mag": vp("GlowMagenta", (0.9, 0.15, 0.6), 200), "Cya": vp("GlowCyan", (0.15, 0.6, 0.9), 300),
        "Glow": sp("Glow", 0.06, 400), "Scale": sp("CloudScale", 0.9, 500), "Wind": vp("Wind", (0.012, 0.004, 0), 600),
        "Oct": sp("Octaves", 4.0, 700), "Cover": sp("CloudCover", 0.38, 800), "CloudLit": sp("CloudLit", 1.6, 900),
        "Intensity": sp("Intensity", 1.0, 1000)}
for n in names: L.connect_material_expressions(srcs[n], "", cu, n)
L.connect_material_property(cu, "", MP.MP_EMISSIVE_COLOR)
L.recompile_material(m); EAL.save_loaded_asset(m)
unreal.log("OMABENCH_SKY_DONE")

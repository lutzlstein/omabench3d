
## Tests and scoring (v0.3)

| Test | Scene | RT profile (applied at runtime per test, recorded in `tests[].rtConfig`) |
|---|---|---|
| T1 City Night | rainy city flythrough | `hw-lumen-default`: HW Lumen GI+reflections, surface-cache lighting (`LightingMode 0`), screen traces on, max roughness 0.4, RT shadows off, translucency front-layer off |
| T2 Off-World Hangar | spinner hangar: chrome, glass, mirror floor, holo signs, window onto a planet | `hwrt-max`: HW Lumen, hit-lighting (`r.Lumen.HardwareRayTracing.LightingMode 2`), screen traces off, max roughness 0.6, translucency front-layer reflections on, `r.RayTracing.Shadows 1` (2 spp) |

`MaxReflectionBounces`/`MaxRefractionBounces` are requested too; cvars the engine lacks are listed in `rtConfig.unsupported` (on UE 5.8.4 both are absent).

Per-test score = 100 x avgFps x clamp(1 - frame-time CoV, 0.8, 1), median over passes. `scores.gpu` = geometric mean of the per-test scores (`scores.tests` has each). Multi-test runs: `-tests=T1,T2` runs them in order, each with its own warmup and passes; progress.json carries `test`, `testIndex`, `testCount`, and a cumulative `elapsedS/totalS`.

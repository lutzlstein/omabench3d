# One editor session for the whole content step: FBX import + CC0 textures/materials/LODs + T3 particle materials (saves ~1 editor boot).
import os, runpy
D = os.path.dirname(os.path.abspath(__file__))
for s in ("import_blender_assets.py", "make_cc0_materials.py", "make_t3_materials.py", "make_sky_material.py"):
    runpy.run_path(os.path.join(D, s), run_name="__main__")

# Headless UE import of the Blender-generated FBX meshes (art/export/*.fbx) into /Game/OmaBench/Meshes.
# Materials are NOT imported: slot names (Paint/Chrome/Glass/Dark/Emit*/Marking/Screen) are kept and the
# game binds OmaBench material instances to them at runtime.
import unreal, os, glob
proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
src = os.path.join(proj, "art", "export")
tasks = []
for f in sorted(glob.glob(os.path.join(src, "*.fbx"))):
    t = unreal.AssetImportTask()
    t.filename = f; t.destination_path = "/Game/OmaBench/Meshes"; t.automated = True; t.replace_existing = True; t.save = True
    o = unreal.FbxImportUI()
    o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False; o.import_animations = False
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    sm = o.static_mesh_import_data
    sm.combine_meshes = True; sm.generate_lightmap_u_vs = False; sm.auto_generate_collision = False
    sm.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    sm.set_editor_property("build_nanite", False)
    t.options = o; tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for t in tasks:
    unreal.log("OMABENCH_IMPORT %s -> %s" % (os.path.basename(t.filename), list(t.imported_object_paths)))
unreal.log("OMABENCH_IMPORT_DONE %d" % len(tasks))

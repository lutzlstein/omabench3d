using UnrealBuildTool;
public class OmaBenchEditor : ModuleRules {
  public OmaBenchEditor(ReadOnlyTargetRules Target) : base(Target) {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Niagara", "NiagaraCore", "NiagaraEditor", "Json", "JsonUtilities", "StructUtils", "AssetTools", "EditorScriptingUtilities" });
  }
}

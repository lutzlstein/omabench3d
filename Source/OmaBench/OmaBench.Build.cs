using UnrealBuildTool;
public class OmaBench : ModuleRules {
  public OmaBench(ReadOnlyTargetRules Target) : base(Target) {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "RHI", "RenderCore" });
  }
}

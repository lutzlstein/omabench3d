using UnrealBuildTool;
public class OmaBenchEditorTarget : TargetRules {
  public OmaBenchEditorTarget(TargetInfo Target) : base(Target) {
    Type = TargetType.Editor;
    DefaultBuildSettings = BuildSettingsVersion.Latest;
    IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
    ExtraModuleNames.AddRange(new[] { "OmaBench", "OmaBenchEditor" });
  }
}

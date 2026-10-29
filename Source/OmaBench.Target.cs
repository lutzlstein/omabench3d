using UnrealBuildTool;
public class OmaBenchTarget : TargetRules {
  public OmaBenchTarget(TargetInfo Target) : base(Target) {
    Type = TargetType.Game;
    DefaultBuildSettings = BuildSettingsVersion.Latest;
    IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
    ExtraModuleNames.Add("OmaBench");
  }
}

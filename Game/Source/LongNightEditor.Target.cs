using UnrealBuildTool;
public class LongNightEditorTarget : TargetRules
{
    public LongNightEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("LongNight");
    }
}

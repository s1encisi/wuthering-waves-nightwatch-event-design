using UnrealBuildTool;
public class LongNight : ModuleRules
{
    public LongNight(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "Json", "JsonUtilities" });
        PrivateDependencyModuleNames.AddRange(new[] { "MovieSceneCapture", "AudioMixer", "AVIWriter" });
    }
}

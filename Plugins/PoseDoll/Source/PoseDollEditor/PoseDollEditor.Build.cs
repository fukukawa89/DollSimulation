using UnrealBuildTool;
public class PoseDollEditor : ModuleRules
{
    public PoseDollEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] { "ImageCore", "ImageWrapper", "InputCore", "RenderCore", "RHI", "RigVMDeveloper" });
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Slate", "SlateCore", "ToolMenus", "Projects", "Json", "JsonUtilities", "PoseDollCore", "PoseDollTransport", "PoseDollRig", "ControlRig", "ControlRigEditor", "ControlRigDeveloper", "RigVM", "LevelSequence", "LevelSequenceEditor", "Sequencer", "MovieScene", "MovieSceneTracks", "MovieSceneTools" });
    }
}

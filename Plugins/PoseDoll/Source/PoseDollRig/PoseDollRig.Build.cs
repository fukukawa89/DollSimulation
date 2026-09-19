using UnrealBuildTool;
public class PoseDollRig : ModuleRules
{
    public PoseDollRig(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ControlRig", "RigVM", "PoseDollCore", "Json" });
    }
}

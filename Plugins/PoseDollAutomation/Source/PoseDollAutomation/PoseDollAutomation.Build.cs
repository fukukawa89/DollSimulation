using UnrealBuildTool;
public class PoseDollAutomation : ModuleRules
{
    public PoseDollAutomation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","ToolsetRegistry","PoseDollEditor","PythonScriptPlugin"});
    }
}

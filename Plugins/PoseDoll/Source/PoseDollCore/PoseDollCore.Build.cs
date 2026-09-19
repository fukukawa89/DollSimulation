using UnrealBuildTool;
public class PoseDollCore : ModuleRules
{
    public PoseDollCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json" });
    }
}

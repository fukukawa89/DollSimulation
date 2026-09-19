using UnrealBuildTool;
public class PoseDollTransport : ModuleRules
{
    public PoseDollTransport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json", "Sockets", "Networking", "PoseDollCore" });
    }
}


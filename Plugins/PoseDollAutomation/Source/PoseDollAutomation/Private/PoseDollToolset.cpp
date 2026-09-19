#include "PoseDollToolset.h"
#include "PoseDollEditorLibrary.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Paths.h"
FString UPoseDollToolset::GetPoseDollStatus() {return UPoseDollEditorLibrary::SessionCommand(TEXT("status"));}
FString UPoseDollToolset::LoadPoseDollFixture(const FString& Filename) {return UPoseDollEditorLibrary::SessionCommand(TEXT("fixture"),Filename);}
FString UPoseDollToolset::ValidatePoseDollRig() {return UPoseDollEditorLibrary::TestRigFixtures();}
FString UPoseDollToolset::PoseDollSession(const FString& Action,const FString& Argument) {return UPoseDollEditorLibrary::SessionCommand(Action,Argument);}
bool UPoseDollToolset::RunPoseDollAcceptance(const FString& Suite)
{
    const TMap<FString,FString> Suites={{TEXT("soak"),TEXT("start_soak_editor.py")},{TEXT("lifecycle"),TEXT("test_lifecycle.py")},{TEXT("capture_reopen"),TEXT("test_capture_reopen.py")},{TEXT("contacts"),TEXT("test_contacts.py")},{TEXT("regression"),TEXT("run_validation.py")}};
    if (!Suites.Contains(Suite) || !IPythonScriptPlugin::Get()) return false;
    FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Scripts")/Suites[Suite]);
    Path.ReplaceInline(TEXT("\\"),TEXT("/"));Path.ReplaceInline(TEXT("'"),TEXT("\\'"));
    return IPythonScriptPlugin::Get()->ExecPythonCommand(*FString::Printf(TEXT("import runpy; runpy.run_path('%s', run_name='__main__')"),*Path));
}
class FPoseDollAutomationModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolsetRegistry::RegisterToolsetClass(UPoseDollToolset::StaticClass());
    }
    virtual void ShutdownModule() override
    {
        UToolsetRegistry::UnregisterToolsetClass(UPoseDollToolset::StaticClass());
    }
};
IMPLEMENT_MODULE(FPoseDollAutomationModule,PoseDollAutomation)

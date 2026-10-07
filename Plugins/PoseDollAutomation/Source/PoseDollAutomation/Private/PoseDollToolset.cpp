#include "PoseDollToolset.h"
#include "PoseDollEditorLibrary.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
FString UPoseDollToolset::GetPoseDollStatus() {return UPoseDollEditorLibrary::SessionCommand(TEXT("status"));}
FString UPoseDollToolset::LoadPoseDollFixture(const FString& Filename) {return UPoseDollEditorLibrary::SessionCommand(TEXT("fixture"),Filename);}
FString UPoseDollToolset::ValidatePoseDollRig() {return UPoseDollEditorLibrary::TestRigFixtures();}
FString UPoseDollToolset::PoseDollSession(const FString& Action,const FString& Argument) {return UPoseDollEditorLibrary::SessionCommand(Action,Argument);}
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

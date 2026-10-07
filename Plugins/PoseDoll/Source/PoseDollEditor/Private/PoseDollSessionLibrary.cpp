#include "PoseDollEditorLibrary.h"
#include "PoseDollSession.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

bool UPoseDollEditorLibrary::BindTarget(ULevelSequence* Sequence,USkeletalMeshComponent* Component) {return PoseDoll::FSession::Get().Bind(Sequence,Component);}
FString UPoseDollEditorLibrary::SessionCommand(const FString& Action,const FString& Argument)
{
    auto& S=PoseDoll::FSession::Get();bool Ok=true;
    if (Action==TEXT("key_report")) return S.KeyReport();
    if (Action==TEXT("pose_report")) return S.PoseReport();
    if (Action==TEXT("connect")) Ok=S.Connect(Argument==TEXT("static")?39178:39177);
    else if (Action==TEXT("disconnect")) S.Disconnect();
    else if (Action==TEXT("fixture")) Ok=S.LoadFixture(Argument);
    else if (Action==TEXT("bind_selection")) Ok=S.BindSelection();
    else if (Action==TEXT("mask"))
    {
        const TArray<FString> Masks={TEXT("FullBody"),TEXT("UpperBody"),TEXT("LowerBody"),TEXT("Custom"),TEXT("arm_l"),TEXT("arm_r"),TEXT("leg_l"),TEXT("leg_r")};
        Ok=Masks.Contains(Argument);if (Ok) S.SetMask(Argument);else S.Error=TEXT("Unknown mask");
    }
    else if(Action==TEXT("custom_parts"))
    {
        TSharedPtr<FJsonObject> O;FString E;const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;
        if(!PoseDoll::ReadJson(Argument,O,E)||!O->TryGetArrayField(TEXT("parts"),Parts)){Ok=false;S.Error=TEXT("Expected a parts array");}
        else{TSet<FString> Values;for(const auto& P:*Parts)Values.Add(P->AsString());Ok=S.SetCustomParts(Values);}
    }
    else if(Action==TEXT("capture_current"))
    {
        int32 Advance=0;bool Linear=false;TSharedPtr<FJsonObject> O;FString E;
        if(!Argument.IsEmpty()&&!PoseDoll::ReadJson(Argument,O,E)){Ok=false;S.Error=E;}
        else{if(O){O->TryGetNumberField(TEXT("advance"),Advance);O->TryGetBoolField(TEXT("linear"),Linear);}Ok=S.CaptureCurrent(Advance,Linear);}
    }
    else if(Action==TEXT("snapshot")||Action==TEXT("snapshot_capture"))
    {
        int32 Advance=0;bool Linear=false;TSharedPtr<FJsonObject> O;FString E;
        if(!Argument.IsEmpty()&&!PoseDoll::ReadJson(Argument,O,E)){Ok=false;S.Error=E;}
        else{if(O){O->TryGetNumberField(TEXT("advance"),Advance);O->TryGetBoolField(TEXT("linear"),Linear);}Ok=S.RequestSnapshot(Action==TEXT("snapshot_capture"),Advance,Linear);}
    }
    else if(Action==TEXT("snapshot_cancel"))S.CancelSnapshot();
    else if (Action==TEXT("capture"))
    {
        TSharedPtr<FJsonObject> O;FString Error;
        if (!PoseDoll::ReadJson(Argument,O,Error)) {Ok=false;S.Error=Error;}
        else {int32 Frame=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame.FrameNumber.Value,Advance=0;bool Linear=false;O->TryGetNumberField(TEXT("frame"),Frame);O->TryGetNumberField(TEXT("advance"),Advance);O->TryGetBoolField(TEXT("linear"),Linear);Ok=S.Capture(Frame,Advance,Linear);}
    }
    else if (Action==TEXT("tick")) S.Tick(0);
    else if (Action==TEXT("undo")) {S.CancelSnapshot();GEditor->UndoTransaction();}
    else if (Action==TEXT("redo")) {S.CancelSnapshot();GEditor->RedoTransaction();}
    else if (Action==TEXT("open_panel")) FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("PoseDollLab")));
    else if (Action==TEXT("close_panel")) {auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FName(TEXT("PoseDollLab")));if (Tab) Tab->RequestCloseTab();}
    else if (Action!=TEXT("status")) {Ok=false;S.Error=TEXT("Unknown session action");}
    TSharedPtr<FJsonObject> Result;FString Error;PoseDoll::ReadJson(S.StatusJson(),Result,Error);Result->SetBoolField(TEXT("ok"),Ok);return PoseDoll::JsonString(Result.ToSharedRef());
}

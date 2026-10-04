#include "PoseDollSession.h"
#include "PoseDollPoseEditing.h"
#include "PoseDollKeying.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace PoseDoll
{
void FSession::ClearRigObservers()
{
    for(auto& Pair:RigObservers)if(auto* Rig=Pair.Key.Get())
    {
        Rig->ControlModified().Remove(Pair.Value.Modified);
        Rig->OnPostForwardsSolve_AnyThread().Remove(Pair.Value.Evaluated);
    }
    RigObservers.Reset();
}
void FSession::RememberRigModes(UControlRig* Rig)
{
    if(auto* Observer=RigObservers.Find(Rig))
        for(auto& Pair:Observer->Modes)Pair.Value=Rig->GetControlValue(Pair.Key).Get<bool>();
}
void FSession::TickRigEditing()
{
    if(bEditingRig)return;
    auto* Current=ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
    if(!Current || Current!=ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence())
    {ClearRigObservers();return;}
    const auto Proxies=UControlRigSequencerEditorLibrary::GetControlRigs(Current);
    if(Proxies.IsEmpty()){ClearRigObservers();return;}
    if(!Initialize())return;
    TSet<TWeakObjectPtr<UControlRig>> Active;
    for(const auto& P:Proxies)
    {
        UControlRig* Rig=P.ControlRig;
        if(!FNativePoseKeys::MatchingEnabled(P.Track) && (P.Track!=Track.Get() || Current!=Sequence.Get()))continue;
        if(!Rig || Rig->IsAdditive() || Rig->GetClass()!=Adapter->GetRig()->GetClass())continue;
        Active.Add(Rig);
        if(RigObservers.Contains(Rig)){RememberRigModes(Rig);continue;}
        FRigObserver& Observer=RigObservers.Add(Rig);
        for(const auto& M:Adapter->GetMapping())
        {
            const FName Mode=FNativePoseEditing::ModeFor(M);
            if(!Mode.IsNone())Observer.Modes.Add(Mode,Rig->GetControlValue(Mode).Get<bool>());
        }
        Observer.Modified=Rig->ControlModified().AddRaw(this,&FSession::OnRigModified);
        Observer.Evaluated=Rig->OnPostForwardsSolve_AnyThread().AddLambda([this](UControlRig* Subject,const FName&)
        {
            // Playback updates the observation only; it never writes animation.
            if(!IsInGameThread() || bEditingRig)return;
            RememberRigModes(Subject);
        });
    }
    for(auto It=RigObservers.CreateIterator();It;++It)if(!Active.Contains(It.Key()))
    {
        if(auto* Rig=It.Key().Get())
        {Rig->ControlModified().Remove(It.Value().Modified);Rig->OnPostForwardsSolve_AnyThread().Remove(It.Value().Evaluated);}
        It.RemoveCurrent();
    }
}
void FSession::OnRigModified(UControlRig* Rig,FRigControlElement* Control,const FRigControlModifiedContext& Context)
{
    if(!IsInGameThread() || GIsTransacting || bEditingRig || bStaticCommit || !Adapter || !Control || Context.SetKey==EControlRigSetKey::Never)return;
    auto* Observer=RigObservers.Find(Rig);
    if(!Observer)return;
    const FName Mode=Control->GetKey().Name;
    bool* Old=Observer->Modes.Find(Mode);
    if(!Old)return;
    const bool Next=Rig->GetControlValue(Mode).Get<bool>(),Previous=*Old;
    if(Next==Previous)return;
    auto* Seq=ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
    if(!Seq || Seq!=ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence())return;
    UMovieSceneControlRigParameterSection* Section=nullptr;
    for(const auto& P:UControlRigSequencerEditorLibrary::GetControlRigs(Seq))if(P.ControlRig==Rig && P.Track && P.Track->GetAllSections().Num()==1)
        Section=Cast<UMovieSceneControlRigParameterSection>(P.Track->GetAllSections()[0]);
    if(!Section || Section->IsReadOnly() || !Section->IsActive() || Seq->GetMovieScene()->IsReadOnly())return;
    const auto Position=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame;
    const auto Rate=Seq->GetMovieScene()->GetDisplayRate();
    if(Context.LocalTime!=FLT_MAX && !FMath::IsNearlyEqual(double(Context.LocalTime),Position.AsDecimal()/Rate.AsDecimal(),1e-4))
        return; // Do not redirect a scripted edit at another time to the playhead.
    TGuardValue<bool> Guard(bEditingRig,true);
    FEditPose Matched;FString Failure;
    if(!FNativePoseEditing::MatchMode(Rig,*Adapter,Mode,Previous,Next,Matched,Failure))
    {
        Rig->SetControlValue<bool>(Mode,Previous,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
        Error=Failure;UE_LOG(LogTemp,Warning,TEXT("PoseDoll: %s"),*Error);return;
    }
    const FFrameNumber Time=FFrameRate::TransformTime(Position,Rate,Seq->GetMovieScene()->GetTickResolution()).RoundToFrame();
    if(!FNativePoseKeys::Validate(Section,Rig,Time,Matched,Failure))
    {Rig->SetControlValue<bool>(Mode,Previous,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);Error=Failure;return;}
    FScopedTransaction Transaction(FText::FromString(TEXT("PoseDoll match IK/FK")),!GEditor->IsTransactionActive());
    Seq->Modify();Section->Modify();Rig->Modify();
    FNativePoseKeys::Write(Section,Rig,Time,Matched,{{Mode,Previous}},EMovieSceneKeyInterpolation::Constant);
    for(const auto& Pair:Matched.Controls)
        Rig->SetControlLocalTransform(Pair.Key,Pair.Value,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
    *Old=Next;Error.Empty();EditingNote=Matched.Warning;
    if(!EditingNote.IsEmpty())
    {
        UE_LOG(LogTemp,Display,TEXT("PoseDoll: %s"),*EditingNote);
        FNotificationInfo Info(FText::FromString(EditingNote));Info.ExpireDuration=8.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
    Rig->Evaluate_AnyThread();
}
}

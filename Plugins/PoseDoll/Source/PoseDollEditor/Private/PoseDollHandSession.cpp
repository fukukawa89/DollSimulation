#include "PoseDollSession.h"
#include "PoseDollHandPresets.h"
#include "PoseDollKeying.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSequencePlayer.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace PoseDoll
{
bool FSession::ApplyHandPreset(const FString& Id,EHandSide Side,bool Linear)
{
    if(!Initialize()||!ValidateTarget(true))return false;
    auto& Library=FHandPresetLibrary::Get();if(!Library.Load(Error))return false;
    const auto* Preset=Library.Find(Id);if(!Preset){Error=TEXT("Unknown hand preset");return false;}
    const FFrameTime Position=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame;
    if(Position.GetSubFrame()!=0){Error=TEXT("Choose a whole display frame before applying a hand preset");return false;}
    if(GEditor->IsTransactionActive()){Error=TEXT("Finish the current edit before applying a hand preset");return false;}
    if(Track.IsValid()&&Track->GetAllSections().Num()!=1){Error=TEXT("Exactly one managed section is required");return false;}
    // A late hardware sample must never overwrite a subsequent explicit hand edit.
    InvalidateSnapshotContext();
    TGuardValue<bool> RigGuard(bEditingRig,true);TGuardValue<bool> StaticGuard(bStaticCommit,true);
    UMovieScene* Movie=Sequence->GetMovieScene();
    const FFrameNumber Time=FFrameRate::TransformTime(Position,Movie->GetDisplayRate(),Movie->GetTickResolution()).RoundToFrame();
    FEditPose Edit;FString Failure;bool Success=true;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("PoseDoll 手部预设")));
        Sequence->Modify();Movie->Modify();
        if(!Track.IsValid())
        {
            const FMovieSceneBindingProxy Proxy(Binding,Sequence.Get());
            Track=Cast<UMovieSceneControlRigParameterTrack>(UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(Component->GetWorld(),Sequence.Get(),Adapter->GetRig()->GetClass(),Proxy));
        }
        if(bRigNeedsRebuild&&Track.IsValid())
        {
            Track->Modify();for(auto* Section:Track->GetAllSections())Section->Modify();
            auto* Fresh=NewObject<UControlRig>(Track.Get(),Adapter->GetRig()->GetClass(),NAME_None,RF_Transactional);
            Fresh->SetObjectBinding(Track->GetControlRig()->GetObjectBinding());
            Fresh->Initialize();Fresh->RequestConstruction();Fresh->Evaluate_AnyThread();
            Track->ReplaceControlRig(Fresh,false);bRigNeedsRebuild=false;
        }
        auto* Section=Track.IsValid()&&Track->GetAllSections().Num()==1?Cast<UMovieSceneControlRigParameterSection>(Track->GetAllSections()[0]):nullptr;
        UControlRig* Rig=Track.IsValid()?Track->GetControlRig():nullptr;
        if(!Rig||!Section){Success=false;Failure=TEXT("A compatible Control Rig section is required");}
        if(Success)
        {
            ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(Position,EUpdatePositionMethod::Jump));
            Rig->Evaluate_AnyThread();Success=FHandPresetLibrary::BuildEdit(Rig,*Preset,Side,Edit,Failure);
        }
        if(Success)Success=FNativePoseKeys::Validate(Section,Rig,Time,Edit,Failure);
        if(Success)
        {
            Track->Modify();Section->Modify();
            FNativePoseKeys::Write(Section,Rig,Time,Edit,{},Linear?EMovieSceneKeyInterpolation::Linear:EMovieSceneKeyInterpolation::Constant);
            ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
            ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(Position,EUpdatePositionMethod::Jump));
            Rig=Track->GetControlRig();Rig->Evaluate_AnyThread();
            for(const auto& P:Edit.Controls)if(!Rig->GetControlLocalTransform(P.Key).Equals(P.Value,1e-3))
            {Success=false;Failure=TEXT("Hand control readback failed: ")+P.Key.ToString();break;}
            if(Success)Success=FHandPresetLibrary::VerifyBones(Rig,Edit.Bones,Failure);
        }
    }
    if(!Success)
    {
        GEditor->UndoTransaction();Error=TEXT("Hand preset rolled back: ")+Failure;
        AfterUndoRedo();ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();return false;
    }
    Pose.Bones=Edit.Bones;
    for(const auto& P:Edit.Controls)Pose.Controls.Add(P.Key,P.Value);
    ++PreviewRevision;RememberRigModes(Track->GetControlRig());
    State=TEXT("HandPresetApplied");Error.Empty();
    const FString SideLabel=Side==EHandSide::Left?TEXT("左手"):Side==EHandSide::Right?TEXT("右手"):TEXT("双手");
    EditingNote=FString::Printf(TEXT("已覆盖当前帧的%s手指姿势，可撤销或继续编辑。"),*SideLabel);
    return true;
}
}

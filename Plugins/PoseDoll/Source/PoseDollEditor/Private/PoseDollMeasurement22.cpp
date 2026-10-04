#include "PoseDollEditorLibrary.h"
#include "PoseDollCore.h"
#include "PoseDollRigAdapter.h"
#include "ControlRigBlueprintLegacy.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"

FString UPoseDollEditorLibrary::SolveMeasuredPose22(const FString& PayloadFile,const FString& TargetProfileFile,bool AllowSyntheticForTesting)
{
    check(IsInGameThread());
    auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("ok"),false);
    FString Error;TSharedPtr<FJsonObject> Data,Target;
    auto Fail=[&](const FString& Message){Result->SetStringField(TEXT("error"),Message);return PoseDoll::JsonString(Result);};
    if(!PoseDoll::LoadJson(PayloadFile,Data,Error)||!PoseDoll::LoadJson(TargetProfileFile,Target,Error))return Fail(Error);
    FString Schema,Status,Basis,ProfileId;bool Eligible=false;double Count=0;
    if(!Data->TryGetStringField(TEXT("schema"),Schema)||Schema!=TEXT("POSEDOLL-O22-UE/1")||
       !Data->TryGetStringField(TEXT("status"),Status)||Status!=TEXT("VALID_MEASUREMENT")||
       !Data->TryGetStringField(TEXT("basis"),Basis)||Basis!=TEXT("RH_X_FORWARD_Y_LEFT_Z_UP")||
       !Data->TryGetNumberField(TEXT("raw_count"),Count)||Count!=46||
       !Data->TryGetBoolField(TEXT("hardware_capture_eligible"),Eligible))
       return Fail(TEXT("Invalid O22 calibrated pose envelope"));
    if(!Eligible&&!AllowSyntheticForTesting)return Fail(TEXT("Unqualified or synthetic input; production import refused"));
    if(!AllowSyntheticForTesting)
    {
        FString Fingerprint,Qualification;
        if(!Target->TryGetStringField(TEXT("rig_runtime_sha256"),Fingerprint)||Fingerprint.Len()!=64||
           !Target->TryGetStringField(TEXT("o22_validation"),Qualification)||Qualification!=TEXT("DIGITAL_CAPTURE_REOPEN_PASS"))
           return Fail(TEXT("Target profile has not passed O22 capture/reopen validation"));
    }
    const TSharedPtr<FJsonObject>* Rotations=nullptr;
    if(!Data->TryGetObjectField(TEXT("semantic_rotations"),Rotations))return Fail(TEXT("Missing semantic rotations"));
    const TArray<FString> Names={TEXT("pelvis"),TEXT("waist"),TEXT("chest"),TEXT("head"),TEXT("clavicle_l"),TEXT("clavicle_r"),
      TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("lowerarm_l"),TEXT("lowerarm_r"),TEXT("hand_l"),TEXT("hand_r"),
      TEXT("thigh_l"),TEXT("thigh_r"),TEXT("calf_l"),TEXT("calf_r"),TEXT("foot_l"),TEXT("foot_r"),TEXT("ball_l"),TEXT("ball_r")};
    if((*Rotations)->Values.Num()!=Names.Num())return Fail(TEXT("Semantic channel set mismatch"));
    PoseDoll::FProfile Source;TArray<PoseDoll::FMatrix44> Matrices;
    for(const auto& Name:Names)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(!(*Rotations)->TryGetArrayField(Name,Values)||Values->Num()!=9)return Fail(TEXT("Invalid rotation: ")+Name);
        PoseDoll::FMatrix44 M=PoseDoll::FMatrix44::Identity();
        for(int32 I=0;I<9;++I)
        {
            double Value;
            if(!(*Values)[I]->TryGetNumber(Value)||!FMath::IsFinite(Value))return Fail(TEXT("Nonfinite rotation"));
            M.M[I/3][I%3]=Value;
        }
        FVector3d Col[3];
        for(int32 I=0;I<3;++I)Col[I]=FVector3d(M.M[0][I],M.M[1][I],M.M[2][I]);
        for(int32 I=0;I<3;++I)for(int32 J=0;J<3;++J)
            if(FMath::Abs(FVector3d::DotProduct(Col[I],Col[J])-(I==J?1.:0.))>1e-6)return Fail(TEXT("Non-orthonormal rotation"));
        if(FVector3d::DotProduct(FVector3d::CrossProduct(Col[0],Col[1]),Col[2])<.999999)return Fail(TEXT("Mirrored rotation"));
        Source.Segments.Add(Name,Matrices.Num());Source.Nodes.AddDefaulted();Matrices.Add(M);
    }
    const auto& Root=Matrices[0];
    for(int32 I=0;I<3;++I)for(int32 J=0;J<3;++J)
        if(FMath::Abs(Root.M[I][J]-(I==J?1.:0.))>1e-8)return Fail(TEXT("O22 root rotation is not measured"));
    FString MeshPath,RigPath;
    if(!Target->TryGetStringField(TEXT("mesh"),MeshPath)||!Target->TryGetStringField(TEXT("rig"),RigPath))return Fail(TEXT("Target assets missing"));
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,*MeshPath);
    UControlRigBlueprint* Rig=LoadObject<UControlRigBlueprint>(nullptr,*RigPath);
    if(!Mesh||!Rig||!Rig->GeneratedClass)return Fail(TEXT("Target mesh or Rig unavailable"));
    PoseDoll::FCuratedAdapter Adapter;
    if(!Adapter.Initialize(Rig->GeneratedClass,Mesh,TargetProfileFile,Error))return Fail(Error);
    PoseDoll::FPoseResult Pose;
    if(!Adapter.Apply(Source,Matrices,Pose,Error))return Fail(Error);
    auto EncodeTransform=[](const FTransform& T)
    {
        auto O=MakeShared<FJsonObject>();
        auto Array=[](std::initializer_list<double> V){TArray<TSharedPtr<FJsonValue>> A;for(double N:V)A.Add(MakeShared<FJsonValueNumber>(N));return A;};
        const auto P=T.GetLocation();const auto Q=T.GetRotation();const auto S=T.GetScale3D();
        O->SetArrayField(TEXT("p"),Array({P.X,P.Y,P.Z}));
        O->SetArrayField(TEXT("q"),Array({Q.X,Q.Y,Q.Z,Q.W}));
        O->SetArrayField(TEXT("s"),Array({S.X,S.Y,S.Z}));return O;
    };
    auto Controls=MakeShared<FJsonObject>(),Bones=MakeShared<FJsonObject>(),Switches=MakeShared<FJsonObject>();
    for(const auto& V:Pose.Controls)Controls->SetObjectField(V.Key.ToString(),EncodeTransform(V.Value));
    for(const auto& V:Pose.Bones)Bones->SetObjectField(V.Key.ToString(),EncodeTransform(V.Value));
    for(const auto& V:Pose.Switches)Switches->SetBoolField(V.Key.ToString(),V.Value);
    Result->SetObjectField(TEXT("controls"),Controls);Result->SetObjectField(TEXT("bones"),Bones);Result->SetObjectField(TEXT("switches"),Switches);
    Result->SetBoolField(TEXT("ok"),true);Result->SetBoolField(TEXT("hardware_capture_eligible"),Eligible);
    Result->SetBoolField(TEXT("contact_correction"),false);Result->SetBoolField(TEXT("physical_source_test"),Eligible);
    Result->SetNumberField(TEXT("maximum_bone_rotation_error_deg"),Pose.MaximumRotationErrorDegrees);
    Result->SetStringField(TEXT("rig_runtime_sha256"),Adapter.Fingerprint);Result->SetStringField(TEXT("mesh"),MeshPath);
    Result->SetStringField(TEXT("root_and_fingers"),TEXT("AUTHORED_IN_UE"));
    return PoseDoll::JsonString(Result);
}



#include "LevelSequence.h"
#include "GameFramework/Actor.h"
#include "ControlRigObjectBinding.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "ScopedTransaction.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "PoseDollPoseEditing.h"
#include "PoseDollKeying.h"
#include "PoseDollSession.h"
#include "MovieSceneSequencePlayer.h"
#include "Editor.h"

FString UPoseDollEditorLibrary::CaptureMeasuredPose22(ULevelSequence* Sequence,UControlRig* ControlRig,int32 Frame,const FString& PayloadFile,const FString& TargetProfileFile,bool AllowSyntheticForTesting,const FString& CaptureMask,const FString& CustomParts)
{
    const FString Solved=SolveMeasuredPose22(PayloadFile,TargetProfileFile,AllowSyntheticForTesting);
    TSharedPtr<FJsonObject> Data;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Solved),Data)||!Data->GetBoolField(TEXT("ok")))return Solved;
    auto Fail=[&](const FString& Message){auto Out=MakeShared<FJsonObject>();Out->SetBoolField(TEXT("ok"),false);Out->SetStringField(TEXT("error"),Message);return PoseDoll::JsonString(Out);};
    if(!Sequence||!ControlRig)return Fail(TEXT("Explicit sequence and Control Rig required"));
    if(GEditor->IsTransactionActive())return Fail(TEXT("Finish the current edit before capturing a pose"));
    auto* Movie=Sequence->GetMovieScene();
    if(!Movie||Movie->IsReadOnly()||ControlRig->IsAdditive())return Fail(TEXT("Read-only or additive Rig capture is unsupported"));
    if(Sequence!=ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() || Sequence!=ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() || ULevelSequenceEditorBlueprintLibrary::IsPlaying())
        return Fail(TEXT("Open and pause the target top-level sequence before capturing"));
    UMovieSceneControlRigParameterSection* Section=nullptr;int32 Matches=0;
    for(const auto& Binding:static_cast<const UMovieScene*>(Movie)->GetBindings())for(auto* Base:Binding.GetTracks())
    {
        auto* Track=Cast<UMovieSceneControlRigParameterTrack>(Base);
        if(!Track||Track->GetControlRig()!=ControlRig)continue;
        if(Track->GetAllSections().Num()!=1)return Fail(TEXT("Select a single-section Control Rig track"));
        for(auto* Other:Binding.GetTracks())if(Other!=Track && (Other->IsA<UMovieSceneControlRigParameterTrack>() || Other->GetClass()->GetName().Contains(TEXT("SkeletalAnimation"))))
            return Fail(TEXT("The binding has competing animation tracks"));
        Section=Cast<UMovieSceneControlRigParameterSection>(Track->GetAllSections()[0]);++Matches;
    }
    if(Matches!=1||!Section)return Fail(TEXT("Control Rig must belong uniquely to the selected sequence"));
    auto* Component=ControlRig->GetObjectBinding().IsValid()?Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()):nullptr;
    if(!Component && ControlRig->GetObjectBinding().IsValid())if(auto* Actor=Cast<AActor>(ControlRig->GetObjectBinding()->GetBoundObject()))Component=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!Component||Component->GetSkeletalMeshAsset()!=LoadObject<USkeletalMesh>(nullptr,*Data->GetStringField(TEXT("mesh"))))return Fail(TEXT("Bound mesh differs from the validated target"));
    FString Error;PoseDoll::FCuratedAdapter Adapter;
    if(!Adapter.Initialize(ControlRig->GetClass(),Component->GetSkeletalMeshAsset(),TargetProfileFile,Error))return Fail(Error);
    const TSet<FString> Masks={TEXT("FullBody"),TEXT("UpperBody"),TEXT("LowerBody"),TEXT("arm_l"),TEXT("arm_r"),TEXT("leg_l"),TEXT("leg_r"),TEXT("Custom")};
    if(!Masks.Contains(CaptureMask))return Fail(TEXT("Unknown capture mask"));
    TArray<FString> Parts;CustomParts.ParseIntoArray(Parts,TEXT(","),true);
    for(FString& Part:Parts)
    {
        Part.TrimStartAndEndInline();
        if(Part==TEXT("pelvis") || !PoseDoll::FSession::PartOptions().ContainsByPredicate([&](const auto& P){return P.Key==Part;}))
            return Fail(TEXT("Unmeasured or unknown O22 capture part: ")+Part);
    }
    TSet<FName> Selected;
    for(const auto& M:Adapter.GetMapping())
    {
        // O22 has no root sensor; keep UE-authored pelvis placement and rotation.
        if(M.Semantic==TEXT("pelvis"))continue;
        if(CaptureMask==TEXT("FullBody") || (CaptureMask==TEXT("UpperBody") && !M.Group.StartsWith(TEXT("leg"))) ||
           (CaptureMask==TEXT("LowerBody") && M.Group.StartsWith(TEXT("leg"))) || CaptureMask==M.Group ||
           (CaptureMask==TEXT("Custom") && Parts.Contains(M.Semantic)))Selected.Add(M.Control);
    }
    PoseDoll::FPoseResult Source;
    for(const auto& V:Data->GetObjectField(TEXT("bones"))->Values)
    {
        const auto O=V.Value->AsObject();const auto P=O->GetArrayField(TEXT("p")),Q=O->GetArrayField(TEXT("q")),S=O->GetArrayField(TEXT("s"));
        Source.Bones.Add(FName(*V.Key),FTransform(FQuat(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber()),FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()),FVector(S[0]->AsNumber(),S[1]->AsNumber(),S[2]->AsNumber())));
    }
    PoseDoll::FSession::Get().InvalidateSnapshotContext();
    ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame),EUpdatePositionMethod::Jump));
    ControlRig->Evaluate_AnyThread();
    PoseDoll::FEditPose Edit;
    if(!PoseDoll::FNativePoseEditing::Capture(ControlRig,Adapter,Source,Selected,Edit,Error))return Fail(Error);
    const FFrameNumber Time=FFrameRate::TransformTime(FFrameTime(Frame),Movie->GetDisplayRate(),Movie->GetTickResolution()).RoundToFrame();
    if(!PoseDoll::FNativePoseKeys::Validate(Section,ControlRig,Time,Edit,Error))return Fail(Error);
    TMap<FName,bool> Previous;for(const auto& V:Edit.Switches)Previous.Add(V.Key,ControlRig->GetControlValue(V.Key).Get<bool>());
    bool Valid=true;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Capture PoseDoll O22 measured pose")));
        Sequence->Modify();Movie->Modify();Section->Modify();
        PoseDoll::FNativePoseKeys::Write(Section,ControlRig,Time,Edit,Previous,EMovieSceneKeyInterpolation::Constant);
        ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
        ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame),EUpdatePositionMethod::Jump));
        ControlRig->Evaluate_AnyThread();
        for(const auto& M:Adapter.GetMapping())
        {
            const FTransform Actual=ControlRig->GetHierarchy()->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
            const FTransform Expected=Edit.Bones[M.Bone];
            if(FVector::Distance(Actual.GetLocation(),Expected.GetLocation())>.1 || FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Expected.GetRotation()))>.5)
            {Valid=false;Error=TEXT("Sequence readback failed: ")+M.Bone.ToString();break;}
        }
    }
    if(!Valid){GEditor->UndoTransaction();return Fail(TEXT("Capture rolled back: ")+Error);}
    Data->SetStringField(TEXT("capture_mask"),CaptureMask);
    TArray<TSharedPtr<FJsonValue>> Written;for(const auto& V:Edit.Controls)Written.Add(MakeShared<FJsonValueString>(V.Key.ToString()));
    Data->SetArrayField(TEXT("written_controls"),Written);Data->SetStringField(TEXT("capture_operation"),TEXT("ReplaceLocalRotation"));
    return PoseDoll::JsonString(Data.ToSharedRef());
}

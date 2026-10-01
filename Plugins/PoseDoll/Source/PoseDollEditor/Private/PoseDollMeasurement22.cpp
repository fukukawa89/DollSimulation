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

FString UPoseDollEditorLibrary::CaptureMeasuredPose22(ULevelSequence* Sequence,UControlRig* ControlRig,int32 Frame,const FString& PayloadFile,const FString& TargetProfileFile,bool AllowSyntheticForTesting)
{
    const FString Solved=SolveMeasuredPose22(PayloadFile,TargetProfileFile,AllowSyntheticForTesting);
    TSharedPtr<FJsonObject> Data;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Solved),Data)||!Data->GetBoolField(TEXT("ok")))return Solved;
    auto Fail=[&](const FString& Message){auto Out=MakeShared<FJsonObject>();Out->SetBoolField(TEXT("ok"),false);Out->SetStringField(TEXT("error"),Message);return PoseDoll::JsonString(Out);};
    if(!Sequence||!ControlRig)return Fail(TEXT("Explicit sequence and Control Rig required"));
    auto* Movie=Sequence->GetMovieScene();
    if(!Movie||Movie->IsReadOnly()||ControlRig->IsAdditive())return Fail(TEXT("Read-only or additive Rig capture is unsupported"));
    UMovieSceneControlRigParameterSection* Section=nullptr;int32 Matches=0;
    for(const auto& Binding:static_cast<const UMovieScene*>(Movie)->GetBindings())for(auto* Base:Binding.GetTracks())
    {
        auto* Track=Cast<UMovieSceneControlRigParameterTrack>(Base);
        if(!Track||Track->GetControlRig()!=ControlRig)continue;
        if(Track->GetAllSections().Num()!=1)return Fail(TEXT("Select a single-section Control Rig track"));
        Section=Cast<UMovieSceneControlRigParameterSection>(Track->GetAllSections()[0]);++Matches;
    }
    if(Matches!=1||!Section)return Fail(TEXT("Control Rig must belong uniquely to the selected sequence"));
    if(Section->IsReadOnly()||!Section->IsActive())return Fail(TEXT("Section must be active and writable"));
    const auto* Warp=Section->GetTimeWarp();
    if(Warp&&!(*Warp==FMovieSceneTimeWarpVariant(1.0)))return Fail(TEXT("Time warp is unsupported"));
    auto* Component=ControlRig->GetObjectBinding().IsValid()?Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()):nullptr;
    if(!Component && ControlRig->GetObjectBinding().IsValid())if(auto* Actor=Cast<AActor>(ControlRig->GetObjectBinding()->GetBoundObject()))Component=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!Component||Component->GetSkeletalMeshAsset()!=LoadObject<USkeletalMesh>(nullptr,*Data->GetStringField(TEXT("mesh"))))return Fail(TEXT("Bound mesh differs from the validated target"));
    const auto Controls=Data->GetObjectField(TEXT("controls")),Switches=Data->GetObjectField(TEXT("switches"));
    for(const auto& V:Controls->Values)if(!Section->HasTransformParameter(FName(*V.Key)))return Fail(FString(TEXT("Missing control channel "))+V.Key);
    for(const auto& V:Switches->Values)if(!Section->HasBoolParameter(FName(*V.Key)))return Fail(FString(TEXT("Missing switch channel "))+V.Key);
    const FFrameNumber Time=FFrameRate::TransformTime(FFrameTime(Frame),Movie->GetDisplayRate(),Movie->GetTickResolution()).RoundToFrame();
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Capture PoseDoll O22 measured pose")));
        Sequence->Modify();Movie->Modify();Section->Modify();Section->ExpandToFrame(Time);
        for(const auto& V:Controls->Values)
        {
            const auto O=V.Value->AsObject();const auto P=O->GetArrayField(TEXT("p")),Q=O->GetArrayField(TEXT("q")),S=O->GetArrayField(TEXT("s"));
            FTransform T(FQuat(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber()),FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()),FVector(S[0]->AsNumber(),S[1]->AsNumber(),S[2]->AsNumber()));
            const FName Name(*V.Key);Section->AddTransformParameterKey(Name,Time,T,EMovieSceneKeyInterpolation::Constant);
            if(Section->CanCreateSpaceChannel(Name))
            {
                Section->AddSpaceChannel(Name,true);
                if(auto* Space=Section->GetSpaceChannel(Name))Space->SpaceCurve.GetData().UpdateOrAddKey(Time,FMovieSceneControlRigSpaceBaseKey());
            }
        }
        for(const auto& V:Switches->Values)Section->AddBoolParameterKey(FName(*V.Key),Time,V.Value->AsBool());
        Section->MarkAsChanged();
    }
    ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
    return Solved;
}

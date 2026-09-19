#include "PoseDollEditorLibrary.h"
#include "PoseDollCore.h"
#include "ControlRig.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRigObjectBinding.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rigs/RigHierarchy.h"
#include "UObject/StrongObjectPtr.h"
#include "PoseDollTransport.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "PoseDollRigAdapter.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
{
    TArray<TSharedPtr<FJsonValue>> A; for (double V:Values) A.Add(MakeShared<FJsonValueNumber>(V)); return A;
}
}
FString UPoseDollEditorLibrary::RunTransportProbe(float Seconds)
{
    auto Result=MakeShared<FJsonObject>();PoseDoll::FProfile P;FString Error;
    if (!P.Load(FPaths::ProjectDir()/TEXT("Shared/Profiles"),Error)) {Result->SetStringField(TEXT("error"),Error);return PoseDoll::JsonString(Result);}
    PoseDoll::FTcpSource Source(P);Source.Start();PoseDoll::FDecoder Decoder;
    uint64 Generation=0,Last=MAX_uint64;int32 Valid=0,Invalid=0;TArray<double> Times,Latency;
    const double End=FPlatformTime::Seconds()+FMath::Clamp(Seconds,1.0f,3600.0f);
    while (FPlatformTime::Seconds()<End)
    {
        const auto State=Source.Snapshot();
        if (State.Generation!=Generation) {Generation=State.Generation;Decoder.Reset();Last=MAX_uint64;}
        if (State.bConnected && State.bHasSample && State.Latest.Sequence!=Last)
        {
            Last=State.Latest.Sequence;TArray<double> Q;TArray<PoseDoll::FMatrix44> Pose;const double Start=FPlatformTime::Seconds();
            if (Decoder.Decode(P,State.Identity,State.Latest,Q,Error) && P.Forward(Q,Pose,Error))
            {++Valid;Times.Add((FPlatformTime::Seconds()-Start)*1000);Latency.Add((Start-State.Latest.ReceivedSeconds)*1000);}
            else ++Invalid;
        }
        FPlatformProcess::Sleep(.008f);
    }
    const auto State=Source.Snapshot();Source.Stop();Times.Sort();Latency.Sort();
    Result->SetStringField(TEXT("status"),Valid>0 ? TEXT("SAMPLES_PROCESSED") : TEXT("FAIL_NO_VALID_SAMPLES"));
    Result->SetNumberField(TEXT("valid"),Valid);Result->SetNumberField(TEXT("invalid"),Invalid);Result->SetNumberField(TEXT("received"),State.Received);Result->SetNumberField(TEXT("rejected"),State.Rejected);Result->SetNumberField(TEXT("generations"),Generation);Result->SetStringField(TEXT("last_error"),Error);Result->SetStringField(TEXT("transport_error"),State.Error);
    if (Times.Num()) {Result->SetNumberField(TEXT("core_processing_p95_ms"),Times[FMath::Min(Times.Num()-1,FMath::FloorToInt(Times.Num()*.95))]);Result->SetNumberField(TEXT("receive_to_process_p95_ms"),Latency[FMath::Min(Latency.Num()-1,FMath::FloorToInt(Latency.Num()*.95))]);}
    return PoseDoll::JsonString(Result);
}
namespace
{
TSharedRef<FJsonObject> TransformJson(const FTransform& T)
{
    auto O=MakeShared<FJsonObject>();const auto P=T.GetTranslation();const auto Q=T.GetRotation();const auto S=T.GetScale3D();
    O->SetArrayField(TEXT("translation"),Numbers({P.X,P.Y,P.Z}));O->SetArrayField(TEXT("rotation_xyzw"),Numbers({Q.X,Q.Y,Q.Z,Q.W}));O->SetArrayField(TEXT("scale"),Numbers({S.X,S.Y,S.Z}));return O;
}
}
FString UPoseDollEditorLibrary::InspectRig(const FString& MeshPath,const FString& RigPath)
{
    check(IsInGameThread());
    auto Result=MakeShared<FJsonObject>();
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,*MeshPath);
    UControlRigBlueprint* Asset=LoadObject<UControlRigBlueprint>(nullptr,*RigPath);
    if (!Mesh || !Asset || !Asset->GeneratedClass) {Result->SetStringField(TEXT("error"),TEXT("Invalid explicit mesh / rig asset"));return PoseDoll::JsonString(Result);}
    TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));Component->SetSkeletalMeshAsset(Mesh);
    TStrongObjectPtr<UControlRig> Rig(NewObject<UControlRig>(GetTransientPackage(),Asset->GeneratedClass,NAME_None,RF_Transient));
    auto Binding=MakeShared<FControlRigObjectBinding>();Binding->BindToObject(Component.Get());Rig->SetObjectBinding(Binding);
    Rig->Initialize();Rig->RequestConstruction();Rig->Evaluate_AnyThread();
    URigHierarchy* H=Rig->GetHierarchy();
    Result->SetStringField(TEXT("rig_class"),Asset->GeneratedClass->GetPathName());
    TArray<TSharedPtr<FJsonValue>> Bones,Controls,MeshBones;
    for (FRigElementKey Key:H->GetBoneKeys())
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Key.Name.ToString());O->SetObjectField(TEXT("current"),TransformJson(H->GetGlobalTransform(Key)));O->SetObjectField(TEXT("initial"),TransformJson(H->GetGlobalTransform(Key,true)));O->SetStringField(TEXT("parent"),H->GetFirstParent(Key).Name.ToString());Bones.Add(MakeShared<FJsonValueObject>(O));
    }
    for (FRigElementKey Key:H->GetControlKeys())
    {
        auto O=MakeShared<FJsonObject>();const auto* E=H->Find<FRigControlElement>(Key);
        O->SetStringField(TEXT("name"),Key.Name.ToString());O->SetNumberField(TEXT("type"),int32(E->Settings.ControlType));
        O->SetObjectField(TEXT("global"),TransformJson(Rig->GetControlGlobalTransform(Key.Name)));O->SetObjectField(TEXT("local"),TransformJson(Rig->GetControlLocalTransform(Key.Name)));
        if (E->Settings.ControlType==ERigControlType::Bool) O->SetBoolField(TEXT("bool"),H->GetControlValue<bool>(Key));
        Controls.Add(MakeShared<FJsonValueObject>(O));
    }
    const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Global;
    for (int32 I=0; I<Ref.GetNum(); ++I)
    {
        const FTransform Local=Ref.GetRefBonePose()[I];const int32 Parent=Ref.GetParentIndex(I);Global.Add(Parent==INDEX_NONE ? Local : Local*Global[Parent]);
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());O->SetNumberField(TEXT("parent_index"),Parent);O->SetObjectField(TEXT("local"),TransformJson(Local));O->SetObjectField(TEXT("global"),TransformJson(Global[I]));MeshBones.Add(MakeShared<FJsonValueObject>(O));
    }
    Result->SetArrayField(TEXT("bones"),Bones);Result->SetArrayField(TEXT("controls"),Controls);Result->SetArrayField(TEXT("mesh_reference"),MeshBones);
    // Probe each FK/IK setting on a transient rig. This never modifies the source asset.
    TArray<TSharedPtr<FJsonValue>> Probes;
    for (bool State:{false,true})
    {
        H->ResetPoseToInitial(ERigElementType::All);
        FRigControlModifiedContext Context;Context.SetKey=EControlRigSetKey::Never;
        for (const TCHAR* Switch:{TEXT("arm_l_fk_ik_switch"),TEXT("arm_r_fk_ik_switch"),TEXT("leg_l_fk_ik_switch"),TEXT("leg_r_fk_ik_switch"),TEXT("spine_fk_ik_switch"),TEXT("neck_fk_ik_switch")})
            Rig->SetControlValue<bool>(FName(Switch),State,false,Context,false);
        Rig->Evaluate_AnyThread();
        const FTransform Before=H->GetGlobalTransform(FRigElementKey(TEXT("lowerarm_l"),ERigElementType::Bone));
        FTransform Value=Rig->GetControlGlobalTransform(TEXT("lowerarm_l_fk_ctrl"));Value.SetRotation(FQuat(FVector::UpVector,.3)*Value.GetRotation());
        Rig->SetControlGlobalTransform(TEXT("lowerarm_l_fk_ctrl"),Value,false,Context,false);Rig->Evaluate_AnyThread();
        const FTransform After=H->GetGlobalTransform(FRigElementKey(TEXT("lowerarm_l"),ERigElementType::Bone));
        auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("switch"),State);O->SetNumberField(TEXT("bone_rotation_change_deg"),FMath::RadiansToDegrees(Before.GetRotation().AngularDistance(After.GetRotation())));Probes.Add(MakeShared<FJsonValueObject>(O));
    }
    Result->SetArrayField(TEXT("fk_switch_probe"),Probes);
    TArray<TSharedPtr<FJsonValue>> Sensitivity;
    for (const TCHAR* Name:{TEXT("body_ctrl"),TEXT("hips_ctrl"),TEXT("spine_01_ctrl"),TEXT("spine_02_ctrl"),TEXT("spine_03_ctrl"),TEXT("neck_01_ctrl"),TEXT("neck_02_ctrl"),TEXT("head_ctrl")})
    {
        H->ResetPoseToInitial(ERigElementType::All);FRigControlModifiedContext Context;Context.SetKey=EControlRigSetKey::Never;
        for (const TCHAR* Switch:{TEXT("arm_l_fk_ik_switch"),TEXT("arm_r_fk_ik_switch"),TEXT("leg_l_fk_ik_switch"),TEXT("leg_r_fk_ik_switch"),TEXT("spine_fk_ik_switch"),TEXT("neck_fk_ik_switch")}) Rig->SetControlValue<bool>(FName(Switch),false,false,Context,false);
        Rig->Evaluate_AnyThread();TMap<FName,FQuat> Before;
        for (const TCHAR* B:{TEXT("pelvis"),TEXT("spine_01"),TEXT("spine_02"),TEXT("spine_03"),TEXT("spine_04"),TEXT("spine_05"),TEXT("neck_01"),TEXT("neck_02"),TEXT("head")}) Before.Add(FName(B),H->GetGlobalTransform(FRigElementKey(B,ERigElementType::Bone)).GetRotation());
        auto Value=Rig->GetControlGlobalTransform(Name);Value.SetRotation(FQuat(FVector::UpVector,.2)*Value.GetRotation());Rig->SetControlGlobalTransform(Name,Value,false,Context,false);Rig->Evaluate_AnyThread();
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("control"),Name);auto Changes=MakeShared<FJsonObject>();
        for (const auto& Pair:Before) Changes->SetNumberField(Pair.Key.ToString(),FMath::RadiansToDegrees(Pair.Value.AngularDistance(H->GetGlobalTransform(FRigElementKey(Pair.Key,ERigElementType::Bone)).GetRotation())));
        O->SetObjectField(TEXT("bone_rotation_changes"),Changes);Sensitivity.Add(MakeShared<FJsonValueObject>(O));
    }
    Result->SetArrayField(TEXT("trunk_probe"),Sensitivity);Result->SetStringField(TEXT("status"),TEXT("READ_ONLY_TRANSIENT_PROBE"));return PoseDoll::JsonString(Result);
}

FString UPoseDollEditorLibrary::TestRigFixtures()
{
    const FString Shared=FPaths::ProjectDir()/TEXT("Shared");const FString TargetPath=Shared/TEXT("Profiles/manny_body_ue582_v1.json");
    auto Result=MakeShared<FJsonObject>();FString Error;TSharedPtr<FJsonObject> Target,Hello,Golden;PoseDoll::FProfile Profile;
    if (!PoseDoll::LoadJson(TargetPath,Target,Error) || !Profile.Load(Shared/TEXT("Profiles"),Error) || !PoseDoll::LoadJson(Shared/TEXT("Fixtures/hello.json"),Hello,Error) || !PoseDoll::LoadJson(Shared/TEXT("Fixtures/golden_vectors.json"),Golden,Error)) {Result->SetStringField(TEXT("error"),Error);return PoseDoll::JsonString(Result);}
    UControlRigBlueprint* Asset=LoadObject<UControlRigBlueprint>(nullptr,*Target->GetStringField(TEXT("rig")));USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,*Target->GetStringField(TEXT("mesh")));
    PoseDoll::FCuratedAdapter Adapter;
    if (!Asset || !Adapter.Initialize(Asset->GeneratedClass,Mesh,TargetPath,Error)) {Result->SetStringField(TEXT("error"),Error);return PoseDoll::JsonString(Result);}
    PoseDoll::FIdentity ID;if (!PoseDoll::Handshake(Profile,*Hello,ID,Error)) {Result->SetStringField(TEXT("error"),Error);return PoseDoll::JsonString(Result);}
    TArray<TSharedPtr<FJsonValue>> Cases;bool AllPassed=true;
    for (const auto& Value:Golden->GetArrayField(TEXT("cases")))
    {
        const FString Fixture=Value->AsObject()->GetStringField(TEXT("fixture"));TSharedPtr<FJsonObject> Raw;PoseDoll::FSample Sample;PoseDoll::FDecoder Decoder;TArray<double> Q;TArray<PoseDoll::FMatrix44> Source;PoseDoll::FPoseResult Pose;
        bool Passed=PoseDoll::LoadJson(Shared/TEXT("Fixtures")/Fixture,Raw,Error) && PoseDoll::ParseSample(Profile,ID,*Raw,Sample,Error) && Decoder.Decode(Profile,ID,Sample,Q,Error) && Profile.Forward(Q,Source,Error);
        const double Start=FPlatformTime::Seconds();if (Passed) Passed=Adapter.Apply(Profile,Source,Pose,Error);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("fixture"),Fixture);Row->SetBoolField(TEXT("passed"),Passed);Row->SetStringField(TEXT("error"),Passed?TEXT(""):Error);Row->SetNumberField(TEXT("rotation_error_deg"),Pose.MaximumRotationErrorDegrees);Row->SetNumberField(TEXT("apply_ms"),(FPlatformTime::Seconds()-Start)*1000);Row->SetStringField(TEXT("worst_bone"),Pose.WorstBone);
        auto Bones=MakeShared<FJsonObject>();for (const auto& Pair:Pose.Bones) Bones->SetObjectField(Pair.Key.ToString(),TransformJson(Pair.Value));Row->SetObjectField(TEXT("bones"),Bones);
        auto Controls=MakeShared<FJsonObject>();for (const auto& Pair:Pose.Controls) Controls->SetObjectField(Pair.Key.ToString(),TransformJson(Pair.Value));Row->SetObjectField(TEXT("controls"),Controls);
        Cases.Add(MakeShared<FJsonValueObject>(Row));AllPassed&=Passed;
    }
    Result->SetArrayField(TEXT("cases"),Cases);Result->SetBoolField(TEXT("passed"),AllPassed);Result->SetStringField(TEXT("target_profile"),Adapter.ProfileId);Result->SetStringField(TEXT("rig_runtime_sha256"),Adapter.Fingerprint);return PoseDoll::JsonString(Result);
}

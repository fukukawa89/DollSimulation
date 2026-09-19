#pragma once
#include "CoreMinimal.h"
#include "PoseDollCore.h"
#include "ControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace PoseDoll
{
struct FControlMap
{
    FName Control,Bone,NeutralChild;
    FString Semantic,From,Group;
    double Weight=1;
    FVector NeutralDirection=FVector::ZeroVector;
    FTransform NeutralGlobal,NeutralLocal,NeutralBone;
};
struct FPoseResult
{
    TMap<FName,FTransform> Controls,Bones;
    TMap<FName,bool> Switches;
    double MaximumRotationErrorDegrees=0;
    FString WorstBone;
    double ContactPositionErrorCm=0,ContactRotationErrorDegrees=0;
    bool bContactsReachable=true,bPoleDegenerate=false;
};
struct FContactGoal
{
    FTransform Target;
    FVector PreviousPole=FVector::ZeroVector;
    bool bLockRotation=true;
};
class POSEDOLLRIG_API FCuratedAdapter
{
public:
    bool Initialize(UClass* RigClass,USkeletalMesh* Mesh,const FString& TargetProfile,FString& Error);
    bool Apply(const FProfile& Profile,const TArray<FMatrix44>& Source,FPoseResult& Out,FString& Error);
    bool Constrain(TMap<FString,FContactGoal>& Goals,FPoseResult& Pose,FString& Error);
    UControlRig* GetRig() const {return Rig.Get();}
    USkeletalMesh* GetMesh() const {return Component.IsValid()?Component->GetSkeletalMeshAsset():nullptr;}
    const TArray<FControlMap>& GetMapping() const {return Mapping;}
    const TMap<FName,bool>& GetSwitches() const {return Switches;}
    const TMap<FName,FTransform>& GetAuthoredControls() const {return AuthoredControls;}
    FString ProfileId,Fingerprint;
private:
    void SetSwitches();
    TStrongObjectPtr<UControlRig> Rig;
    TStrongObjectPtr<USkeletalMeshComponent> Component;
    TArray<FControlMap> Mapping;
    TMap<FName,bool> Switches;
    TMap<FName,FTransform> NeutralBones,NeutralSpaces;
    TMap<FName,FTransform> AuthoredControls;
    TMap<FName,FName> BoneParents,SpaceBones;
    TArray<FName> BoneOrder;
    FQuat Basis=FQuat::Identity;
    FRigControlModifiedContext Context;
};
}

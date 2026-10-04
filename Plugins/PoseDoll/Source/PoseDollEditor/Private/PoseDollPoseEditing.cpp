#include "PoseDollPoseEditing.h"
#include "ControlRigObjectBinding.h"
#include "Units/Execution/RigUnit_InverseExecution.h"

namespace PoseDoll
{
namespace
{
TStrongObjectPtr<UControlRig> CloneRig(UControlRig* Source)
{
    TStrongObjectPtr<UControlRig> Copy(NewObject<UControlRig>(GetTransientPackage(),Source->GetClass(),NAME_None,RF_Transient));
    Copy->SetObjectBinding(Source->GetObjectBinding());
    Copy->Initialize();Copy->RequestConstruction();Copy->Evaluate_AnyThread();
    Copy->GetHierarchy()->CopyPose(Source->GetHierarchy(),true,true,true);
    return Copy;
}
TMap<FName,FTransform> Bones(UControlRig* Rig)
{
    TMap<FName,FTransform> Result;
    for(const auto& Key:Rig->GetHierarchy()->GetBoneKeys())Result.Add(Key.Name,Rig->GetHierarchy()->GetGlobalTransform(Key));
    return Result;
}
void SetBool(UControlRig* Rig,FName Name,bool Value)
{
    Rig->SetControlValue<bool>(Name,Value,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
}
void AlignBone(UControlRig* Rig,const FControlMap& M,const FTransform& Desired)
{
    // Preserve authored control/bone offsets, including mirrored controls.
    for(int32 Pass=0;Pass<2;++Pass)
    {
        const FTransform Bone=Rig->GetHierarchy()->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
        FTransform Control=Rig->GetControlGlobalTransform(M.Control);
        const FQuat Delta=(Desired.GetRotation()*Bone.GetRotation().Inverse()).GetNormalized();
        Control.SetRotation((Delta*Control.GetRotation()).GetNormalized());
        Control.SetLocation(Desired.GetLocation()+Delta.RotateVector(Control.GetLocation()-Bone.GetLocation()));
        Rig->SetControlGlobalTransform(M.Control,Control,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
        Rig->Evaluate_AnyThread();
    }
}
bool SameBone(const FTransform& A,const FTransform& B,FString& Error,FName Bone)
{
    const double Angle=FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation()));
    const double Distance=FVector::Distance(A.GetLocation(),B.GetLocation());
    if(Angle>.5 || Distance>.1)
    {
        Error=FString::Printf(TEXT("Pose matching failed at %s: %.4f degrees, %.4f cm; no keys written"),*Bone.ToString(),Angle,Distance);
        return false;
    }
    return true;
}
TArray<FName> IKControls(FName Mode)
{
    const FString Name=Mode.ToString();
    const FString Side=Name.Contains(TEXT("_l_"))?TEXT("l"):TEXT("r");
    TArray<FName> Out;
    auto Add=[&](const FString& N){Out.Add(FName(*N));};
    if(Name.StartsWith(TEXT("arm_")))
    {
        Add(TEXT("hand_")+Side+TEXT("_ik_ctrl"));Add(TEXT("arm_")+Side+TEXT("_pv_ik_ctrl"));
        Add(TEXT("hand_")+Side+TEXT("_ikorient_ctrl"));
    }
    else if(Name.StartsWith(TEXT("leg_")))
    {
        Add(TEXT("foot_")+Side+TEXT("_ik_ctrl"));Add(TEXT("leg_")+Side+TEXT("_pv_ik_ctrl"));
        Add(TEXT("ball_")+Side+TEXT("_ik_ctrl"));Add(TEXT("foot_bk1_")+Side+TEXT("_ctrl"));
        Add(TEXT("foot_roll_")+Side+TEXT("_ctrl"));Add(TEXT("tip_")+Side+TEXT("_ctrl"));Add(TEXT("heel_")+Side+TEXT("_ctrl"));
    }
    else if(Name==TEXT("spine_fk_ik_switch"))
        Out={TEXT("hips_ctrl"),TEXT("hips_tan_ctrl"),TEXT("chest_ctrl"),TEXT("chest_tan_ctrl")};
    else if(Name==TEXT("neck_fk_ik_switch"))Out={TEXT("head_ik_ctrl")};
    return Out;
}
}
FName FNativePoseEditing::ModeFor(const FControlMap& M)
{
    if(M.Group==TEXT("pelvis") || M.Control.ToString().StartsWith(TEXT("clavicle_")))return NAME_None;
    const FString Group=M.Group==TEXT("torso")?TEXT("spine"):M.Group==TEXT("head")?TEXT("neck"):M.Group;
    return FName(*(Group+TEXT("_fk_ik_switch")));
}
bool FNativePoseEditing::Capture(UControlRig* Current,const FCuratedAdapter& Adapter,const FPoseResult& Source,
    const TSet<FName>& Selected,FEditPose& Out,FString& Error)
{
    Out=FEditPose();
    if(!Current || Selected.IsEmpty()){Error=TEXT("Select at least one capture part");return false;}
    auto Work=CloneRig(Current);
    Work->Evaluate_AnyThread();
    const auto Before=Bones(Work.Get());
    TSet<FName> Converted;
    for(const auto& M:Adapter.GetMapping())if(Selected.Contains(M.Control))
    {
        const FName Mode=ModeFor(M);
        if(!Mode.IsNone() && Work->GetControlValue(Mode).Get<bool>())Converted.Add(Mode);
    }
    for(FName Mode:Converted){SetBool(Work.Get(),Mode,false);Out.Switches.Add(Mode,false);}
    Work->Evaluate_AnyThread();
    // Match the complete affected chain before replacing individual joints.
    for(const auto& M:Adapter.GetMapping())if(Converted.Contains(ModeFor(M)))
    {
        AlignBone(Work.Get(),M,Before[M.Bone]);
        Out.Controls.Add(M.Control,Work->GetControlLocalTransform(M.Control));
    }
    TSet<FName> SelectedBones;
    for(const auto& M:Adapter.GetMapping())if(Selected.Contains(M.Control))SelectedBones.Add(M.Bone);
    auto AffectedBySelection=[&](FName Bone)
    {
        FRigElementKey Key(Bone,ERigElementType::Bone);
        while(Key.IsValid())
        {
            if(SelectedBones.Contains(Key.Name))return true;
            Key=Work->GetHierarchy()->GetFirstParent(Key);
        }
        return false;
    };
    // The mannequin FK solver aims each control at its child. Matching a spline
    // IK endpoint can therefore perturb its predecessor. Give unselected joints
    // priority; the selected joint and its descendants may follow that matching.
    for(int32 Pass=0;Pass<2;++Pass)
        for(const auto& M:Adapter.GetMapping())if(Converted.Contains(ModeFor(M)) && !Selected.Contains(M.Control))
            AlignBone(Work.Get(),M,Before[M.Bone]);
    for(const auto& M:Adapter.GetMapping())if(Converted.Contains(ModeFor(M)))
        Out.Controls[M.Control]=Work->GetControlLocalTransform(M.Control);
    for(const auto& M:Adapter.GetMapping())if(!AffectedBySelection(M.Bone))
        if(!SameBone(Before[M.Bone],Work->GetHierarchy()->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone)),Error,M.Bone))return false;
    for(const auto& M:Adapter.GetMapping())if(Selected.Contains(M.Control))
    {
        if(!Source.Bones.Contains(M.Bone)){Error=TEXT("Missing source bone ")+M.Bone.ToString();return false;}
        const FRigElementKey Key(M.Bone,ERigElementType::Bone);
        const FRigElementKey Parent=Work->GetHierarchy()->GetFirstParent(Key);
        const FTransform* SourceParent=Source.Bones.Find(Parent.Name);
        const FQuat Local=SourceParent?Source.Bones[M.Bone].GetRelativeTransform(*SourceParent).GetRotation():Source.Bones[M.Bone].GetRotation();
        FTransform Desired=Work->GetHierarchy()->GetGlobalTransform(Key);
        const FTransform ParentTransform=Parent.IsValid()?Work->GetHierarchy()->GetGlobalTransform(Parent):FTransform::Identity;
        Desired.SetRotation((ParentTransform.GetRotation()*Local).GetNormalized());
        const FTransform OldControl=Work->GetControlLocalTransform(M.Control);
        AlignBone(Work.Get(),M,Desired);
        FTransform Value=Work->GetControlLocalTransform(M.Control);
        // A selected joint supplies orientation, not location or bone scale.
        Value.SetScale3D(OldControl.GetScale3D());
        Work->SetControlLocalTransform(M.Control,Value,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
        Work->Evaluate_AnyThread();
        if(!SameBone(Desired,Work->GetHierarchy()->GetGlobalTransform(Key),Error,M.Bone))return false;
        Out.Controls.Add(M.Control,Value);
        if(!Converted.Contains(ModeFor(M)) && Value.GetLocation().Equals(OldControl.GetLocation(),1e-4))
            Out.RotationOnly.Add(M.Control);
    }
    Out.Bones=Bones(Work.Get());
    for(const auto& M:Adapter.GetMapping())if(Selected.Contains(M.Control))
    {
        const FRigElementKey Key(M.Bone,ERigElementType::Bone);
        const FRigElementKey Parent=Work->GetHierarchy()->GetFirstParent(Key);
        const FTransform* SourceParent=Source.Bones.Find(Parent.Name);
        const FQuat Expected=SourceParent?Source.Bones[M.Bone].GetRelativeTransform(*SourceParent).GetRotation():Source.Bones[M.Bone].GetRotation();
        const FQuat Actual=Work->GetHierarchy()->GetLocalTransform(Key).GetRotation();
        if(FMath::RadiansToDegrees(Expected.AngularDistance(Actual))>.5)
        {Error=TEXT("Rig constraints changed the requested joint rotation: ")+M.Bone.ToString();return false;}
    }
    for(const auto& M:Adapter.GetMapping())if(!AffectedBySelection(M.Bone))
        if(!SameBone(Before[M.Bone],Out.Bones[M.Bone],Error,M.Bone))return false;
    return true;
}
bool FNativePoseEditing::MatchMode(UControlRig* Current,const FCuratedAdapter& Adapter,FName Mode,
    bool PreviousIK,bool NextIK,FEditPose& Out,FString& Error)
{
    Out=FEditPose();
    auto Work=CloneRig(Current);
    SetBool(Work.Get(),Mode,PreviousIK);Work->Evaluate_AnyThread();
    const auto Before=Bones(Work.Get());
    if(!NextIK)
    {
        SetBool(Work.Get(),Mode,false);Work->Evaluate_AnyThread();
        for(int32 MatchPass=0;MatchPass<4;++MatchPass)
        for(const auto& M:Adapter.GetMapping())if(ModeFor(M)==Mode)
        {
            AlignBone(Work.Get(),M,Before[M.Bone]);
            Out.Controls.Add(M.Control,Work->GetControlLocalTransform(M.Control));
        }
    }
    else
    {
        const FRigPose Original=Work->GetHierarchy()->GetPose();
        SetBool(Work.Get(),Mode,true);
        if(!Work->Execute(FRigUnit_InverseExecution::EventName)){Error=TEXT("This Rig has no working Backwards Solve");return false;}
        for(FName Name:IKControls(Mode))
        {
            if(!Work->FindControl(Name)){Error=TEXT("Missing IK control ")+Name.ToString();return false;}
            Out.Controls.Add(Name,Work->GetControlLocalTransform(Name));
        }
        Work->GetHierarchy()->SetPose(Original);
        SetBool(Work.Get(),Mode,true);
        for(const auto& Pair:Out.Controls)Work->SetControlLocalTransform(Pair.Key,Pair.Value,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
        Work->Evaluate_AnyThread();
        // The stock backwards solve derives its pole from a reference rotation.
        // For a bent limb, the current joint positions give the actual bend plane.
        // Retain the native pole for an almost straight limb (plane is undefined).
        const FString Group=Mode.ToString().LeftChop(FString(TEXT("_fk_ik_switch")).Len());
        if(Group.StartsWith(TEXT("arm_")) || Group.StartsWith(TEXT("leg_")))
        {
            const FString Side=Group.Right(1);
            const bool Arm=Group.StartsWith(TEXT("arm_"));
            const FName A(*(FString(Arm?TEXT("upperarm_"):TEXT("thigh_"))+Side));
            const FName B(*(FString(Arm?TEXT("lowerarm_"):TEXT("calf_"))+Side));
            const FName C(*(FString(Arm?TEXT("hand_"):TEXT("foot_"))+Side));
            const FVector Start=Before[A].GetLocation(),Middle=Before[B].GetLocation(),End=Before[C].GetLocation();
            const FVector Axis=(End-Start).GetSafeNormal();
            const FVector Bend=Middle-(Start+Axis*FVector::DotProduct(Middle-Start,Axis));
            if(Bend.Size()>.5 && !Axis.IsNearlyZero())
            {
                const FName Pole(*(Group+TEXT("_pv_ik_ctrl")));
                const FTransform OldPole=Work->GetControlLocalTransform(Pole);
                const double OldDistance=FVector::DistSquared(Middle,Bones(Work.Get())[B].GetLocation());
                FTransform Value=Work->GetControlGlobalTransform(Pole);
                Value.SetLocation(Middle+Bend.GetSafeNormal()*100.0);
                Work->SetControlGlobalTransform(Pole,Value,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
                Work->Evaluate_AnyThread();
                if(FVector::DistSquared(Middle,Bones(Work.Get())[B].GetLocation())>OldDistance+1e-6)
                {
                    Work->SetControlLocalTransform(Pole,OldPole,false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
                    Work->Evaluate_AnyThread();
                }
            }
        }
        for(FName Name:IKControls(Mode))Out.Controls[Name]=Work->GetControlLocalTransform(Name);
    }
    Out.Bones=Bones(Work.Get());Out.Switches.Add(Mode,NextIK);
    double MaxAngle=0,MaxDistance=0;
    for(const auto& M:Adapter.GetMapping())
    {
        if(ModeFor(M)!=Mode)
        {
            if(!SameBone(Before[M.Bone],Out.Bones[M.Bone],Error,M.Bone))return false;
        }
        else
        {
            MaxAngle=FMath::Max(MaxAngle,FMath::RadiansToDegrees(Before[M.Bone].GetRotation().AngularDistance(Out.Bones[M.Bone].GetRotation())));
            MaxDistance=FMath::Max(MaxDistance,FVector::Distance(Before[M.Bone].GetLocation(),Out.Bones[M.Bone].GetLocation()));
        }
    }
    // A two-bone / spline IK has fewer degrees of freedom than arbitrary FK.
    // Rebuild from the latest pose, never resurrect old targets, but report its
    // approximation instead of pretending every FK rotation is representable.
    if(MaxAngle>.5 || MaxDistance>.1)
        Out.Warning=FString::Printf(TEXT("%s 已按当前姿势重建；原生 Rig 的两种解算无法完全表达同一姿势（最大偏差 %.2f° / %.2f cm）。可撤销恢复。"),NextIK?TEXT("IK"):TEXT("FK"),MaxAngle,MaxDistance);
    return true;
}
}

#include "PoseDollRigAdapter.h"
#include "ControlRigObjectBinding.h"
#include "Rigs/RigHierarchy.h"
#include "Engine/SkeletalMesh.h"

namespace PoseDoll
{
void FCuratedAdapter::SetSwitches()
{
    for (const auto& Pair:Switches) Rig->SetControlValue<bool>(Pair.Key,Pair.Value,false,Context,false);
}
bool FCuratedAdapter::Initialize(UClass* Class,USkeletalMesh* Mesh,const FString& Path,FString& Error)
{
    check(IsInGameThread());
    TSharedPtr<FJsonObject> Config;
    if (!Class || !Class->IsChildOf(UControlRig::StaticClass()) || !Mesh || !LoadJson(Path,Config,Error)) {Error=TEXT("Invalid curated rig inputs: ")+Error;return false;}
    ProfileId=Config->GetStringField(TEXT("profile_id"));
    Context.SetKey=EControlRigSetKey::Never;
    Basis=FQuat(FVector::UpVector,FMath::DegreesToRadians(Config->GetNumberField(TEXT("source_basis_to_mesh_yaw_degrees"))));
    Component.Reset(NewObject<USkeletalMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));Component->SetSkeletalMeshAsset(Mesh);
    Rig.Reset(NewObject<UControlRig>(GetTransientPackage(),Class,NAME_None,RF_Transient));
    auto Binding=MakeShared<FControlRigObjectBinding>();Binding->BindToObject(Component.Get());Rig->SetObjectBinding(Binding);
    Rig->Initialize();Rig->RequestConstruction();Rig->Evaluate_AnyThread();
    URigHierarchy* H=Rig->GetHierarchy();
    FString Signature;
    for (const auto& Key:H->GetAllKeys())
    {
        Signature+=Key.ToString()+TEXT("|")+H->GetFirstParent(Key).ToString()+TEXT("|");
        if (Key.Type==ERigElementType::Bone || Key.Type==ERigElementType::Control || Key.Type==ERigElementType::Null)
        {
            const auto V=H->GetGlobalTransform(Key,true);const auto T=V.GetTranslation(),S=V.GetScale3D();const auto Q=V.GetRotation();
            Signature+=FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),T.X,T.Y,T.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
        }
        if (const auto* Control=H->Find<FRigControlElement>(Key)) Signature+=FString::FromInt(int32(Control->Settings.ControlType));
        Signature+=TEXT("\n");
    }
    Fingerprint=Sha256Text(Signature);FString ExpectedFingerprint;
    if (Config->TryGetStringField(TEXT("rig_runtime_sha256"),ExpectedFingerprint) && ExpectedFingerprint!=Fingerprint) {Error=TEXT("Rig hierarchy/reference/offset fingerprint changed; revalidate target profile");return false;}
    Mapping.Reset();Switches.Reset();
    for (const auto& Item:Config->GetObjectField(TEXT("switches"))->Values)
    {
        const FName Name(*Item.Key);const auto* Control=H->Find<FRigControlElement>(FRigElementKey(Name,ERigElementType::Control));
        if (!Control || Control->Settings.ControlType!=ERigControlType::Bool) {Error=TEXT("Rig switch type changed: ")+Name.ToString();return false;}
        Switches.Add(Name,Item.Value->AsBool());
    }
    SetSwitches();Rig->Evaluate_AnyThread();
    for (const auto& Key:H->GetControlKeys())
        if (H->Find<FRigControlElement>(Key)->Settings.ControlType==ERigControlType::EulerTransform)
            AuthoredControls.Add(Key.Name,Rig->GetControlLocalTransform(Key.Name));
    for (const auto& Item:Config->GetArrayField(TEXT("controls")))
    {
        const auto O=Item->AsObject();FControlMap M;
        M.Control=FName(*O->GetStringField(TEXT("control")));M.Bone=FName(*O->GetStringField(TEXT("bone")));M.Semantic=O->GetStringField(TEXT("semantic"));M.Group=O->GetStringField(TEXT("group"));
        O->TryGetStringField(TEXT("from"),M.From);O->TryGetNumberField(TEXT("weight"),M.Weight);
        const auto* Control=H->Find<FRigControlElement>(FRigElementKey(M.Control,ERigElementType::Control));
        if (!Control || Control->Settings.ControlType!=ERigControlType::EulerTransform || !H->Contains(FRigElementKey(M.Bone,ERigElementType::Bone))) {Error=TEXT("Rig mapping no longer matches: ")+M.Control.ToString();return false;}
        FString Child;
        if (O->TryGetStringField(TEXT("neutral_child"),Child))
        {
            M.NeutralChild=FName(*Child);const auto& Direction=O->GetArrayField(TEXT("neutral_direction"));M.NeutralDirection=FVector(Direction[0]->AsNumber(),Direction[1]->AsNumber(),Direction[2]->AsNumber()).GetSafeNormal();
            if (!H->Contains(FRigElementKey(M.NeutralChild,ERigElementType::Bone))) {Error=TEXT("Missing calibration child ")+Child;return false;}
        }
        Mapping.Add(M);
    }
    // N-pose calibration uses target lengths, never source doll lengths. Align each target chain
    // longitudinal direction in order; retain authored control-to-bone orientation and mirrored scale.
    for (auto& M:Mapping)
    {
        if (!M.NeutralChild.IsNone())
        {
            const FTransform Bone=H->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
            const FVector Child=H->GetGlobalTransform(FRigElementKey(M.NeutralChild,ERigElementType::Bone)).GetLocation();
            const FQuat Alignment=FQuat::FindBetweenNormals((Child-Bone.GetLocation()).GetSafeNormal(),M.NeutralDirection);
            FTransform Control=Rig->GetControlGlobalTransform(M.Control);Control.SetRotation(Alignment*Control.GetRotation());
            Rig->SetControlGlobalTransform(M.Control,Control,false,Context,false);Rig->Evaluate_AnyThread();
        }
    }
    // Palm twist uses three independent metacarpal landmarks, not just a bone endpoint.
    for (const FString Side:{FString(TEXT("l")),FString(TEXT("r"))})
    {
        const FVector Hand=H->GetGlobalTransform(FRigElementKey(FName(*(TEXT("hand_")+Side)),ERigElementType::Bone)).GetLocation();
        const FVector Index=H->GetGlobalTransform(FRigElementKey(FName(*(TEXT("index_01_")+Side)),ERigElementType::Bone)).GetLocation()-Hand;
        const FVector Pinky=H->GetGlobalTransform(FRigElementKey(FName(*(TEXT("pinky_01_")+Side)),ERigElementType::Bone)).GetLocation()-Hand;
        FVector Normal=FVector::CrossProduct(Index,Pinky)*(Side==TEXT("l")?-1.0:1.0);Normal.Z=0;Normal.Normalize();
        const double Angle=FMath::Atan2(FVector::CrossProduct(Normal,FVector::YAxisVector).Z,FVector::DotProduct(Normal,FVector::YAxisVector));
        const FName Name(*(TEXT("hand_")+Side+TEXT("_fk_ctrl")));auto Value=Rig->GetControlGlobalTransform(Name);Value.SetRotation(FQuat(FVector::UpVector,Angle)*Value.GetRotation());
        Rig->SetControlGlobalTransform(Name,Value,false,Context,false);Rig->Evaluate_AnyThread();
    }
    for (auto& M:Mapping)
    {
        M.NeutralGlobal=Rig->GetControlGlobalTransform(M.Control);M.NeutralLocal=Rig->GetControlLocalTransform(M.Control);M.NeutralBone=H->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
    }
    NeutralBones.Reset();BoneParents.Reset();BoneOrder.Reset();
    for (const auto& Key:H->GetBoneKeys()) {NeutralBones.Add(Key.Name,H->GetGlobalTransform(Key));BoneParents.Add(Key.Name,H->GetFirstParent(Key).Name);BoneOrder.Add(Key.Name);}
    SpaceBones={{TEXT("chest_space"),TEXT("spine_05")},{TEXT("upperarm_l_fk_ctrl_space"),TEXT("clavicle_l")},{TEXT("upperarm_r_fk_ctrl_space"),TEXT("clavicle_r")},{TEXT("thigh_l_fk_ctrl_space"),TEXT("pelvis")},{TEXT("thigh_r_fk_ctrl_space"),TEXT("pelvis")},{TEXT("head_fk_space"),TEXT("head")}};
    for (const auto& Pair:SpaceBones) NeutralSpaces.Add(Pair.Key,H->GetGlobalTransform(FRigElementKey(Pair.Key,ERigElementType::Null)));
    return true;
}
bool FCuratedAdapter::Apply(const FProfile& P,const TArray<FMatrix44>& Source,FPoseResult& Out,FString& Error)
{
    check(IsInGameThread());
    if (!Rig.IsValid() || Source.Num()!=P.Nodes.Num()) {Error=TEXT("Rig/source not initialized");return false;}
    URigHierarchy* H=Rig->GetHierarchy();Out=FPoseResult();SetSwitches();
    for (const auto& Pair:NeutralSpaces) H->SetGlobalTransform(FRigElementKey(Pair.Key,ERigElementType::Null),Pair.Value,false,true);
    // Restore the calibrated controls; source poses are absolute and never accumulate frame deltas.
    for (const auto& M:Mapping) Rig->SetControlLocalTransform(M.Control,M.NeutralLocal,false,Context,false);
    auto SourceRotation=[&](const FString& Segment)
    {
        const FQuat Q=Source[P.Segments[Segment]].ToUnreal().GetRotation();return Basis*Q*Basis.Inverse();
    };
    TMap<FName,FQuat> Expected;
    TMap<FName,FQuat> Deltas;
    for (const auto& M:Mapping)
    {
        if (!P.Segments.Contains(M.Semantic) || (!M.From.IsEmpty() && !P.Segments.Contains(M.From))) {Error=TEXT("Semantic mapping changed");return false;}
        FQuat Delta=SourceRotation(M.Semantic);
        if (!M.From.IsEmpty()) Delta=FQuat::Slerp(SourceRotation(M.From),Delta,M.Weight).GetNormalized();
        Deltas.Add(M.Control,Delta);
        Expected.Add(M.Bone,(Delta*M.NeutralBone.GetRotation()).GetNormalized());
    }
    // Predict the authored torso-driven spaces from calibrated bone lengths. This is
    // the dependency state needed for control conversion, not a write to final bones.
    TMap<FName,FTransform> TorsoBones;
    for (const FName Bone:BoneOrder)
    {
        const FName Parent=BoneParents[Bone];
        FTransform T=NeutralBones[Bone];
        if (TorsoBones.Contains(Parent)) T=T.GetRelativeTransform(NeutralBones[Parent])*TorsoBones[Parent];
        for (const auto& M:Mapping)
        {
            if (M.Bone==Bone) T.SetRotation(Expected[Bone]);
            if (M.Control==TEXT("spine_03_ctrl")) break;
        }
        TorsoBones.Add(Bone,T);
    }
    // Resolve spaces after torso controls have been set; then one native Forward
    // Solve owns the final deformation and the strict control/bone readback.
    for (const auto& M:Mapping)
    {
        FTransform Control=Rig->GetControlGlobalTransform(M.Control);const FQuat Delta=Deltas[M.Control];
        Control.SetRotation((Delta*M.NeutralGlobal.GetRotation()).GetNormalized());
        if (M.Group==TEXT("pelvis")) Control.SetLocation(M.NeutralBone.GetLocation()+Delta.RotateVector(M.NeutralGlobal.GetLocation()-M.NeutralBone.GetLocation()));
        Rig->SetControlGlobalTransform(M.Control,Control,false,Context,false);
        if (M.Control==TEXT("spine_03_ctrl"))
        {
            for (const auto& Pair:SpaceBones)
            {
                FTransform Space=NeutralSpaces[Pair.Key].GetRelativeTransform(NeutralBones[Pair.Value])*TorsoBones[Pair.Value];
                // Arm/head "local" orientation follows body_ctrl, not the chest
                // or clavicle; its pivot still follows the articulated torso bones.
                if (Pair.Key==TEXT("head_fk_space") || Pair.Key.ToString().StartsWith(TEXT("upperarm_"))) Space.SetRotation((Deltas[TEXT("body_ctrl")]*NeutralSpaces[Pair.Key].GetRotation()).GetNormalized());
                H->SetGlobalTransform(FRigElementKey(Pair.Key,ERigElementType::Null),Space,false,true);
            }
        }
        if (M.Control==TEXT("clavicle_r_ctrl"))
        {
            // The arm FK spaces follow the two clavicle bones with a fixed authored
            // offset. Update those dependency transforms before the final native solve.
            // This avoids evaluating the entire deformation rig only to update two nulls.
            for (const auto& Clav:Mapping) if (Clav.Control==TEXT("clavicle_l_ctrl") || Clav.Control==TEXT("clavicle_r_ctrl"))
            {
                const FName Space(*(FString(TEXT("upperarm_"))+Clav.Control.ToString().Mid(9,1)+TEXT("_fk_ctrl_space")));
                FTransform Bone=TorsoBones[Clav.Bone];
                Bone.SetRotation((Deltas[Clav.Control]*Clav.NeutralBone.GetRotation()).GetNormalized());
                const FTransform Offset=NeutralSpaces[Space].GetRelativeTransform(Clav.NeutralBone);
                FTransform TargetSpace=Offset*Bone;
                TargetSpace.SetRotation(H->GetGlobalTransform(FRigElementKey(Space,ERigElementType::Null)).GetRotation());
                H->SetGlobalTransform(FRigElementKey(Space,ERigElementType::Null),TargetSpace,false,true);
            }
        }
    }
    Rig->Evaluate_AnyThread();
    for (const auto& M:Mapping)
    {
        Out.Controls.Add(M.Control,Rig->GetControlLocalTransform(M.Control));
        const FTransform Bone=H->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
        const double ErrorDeg=FMath::RadiansToDegrees(Expected[M.Bone].AngularDistance(Bone.GetRotation()));
        if (ErrorDeg>Out.MaximumRotationErrorDegrees) {Out.MaximumRotationErrorDegrees=ErrorDeg;Out.WorstBone=M.Bone.ToString();}
    }
    for (const auto& Key:H->GetBoneKeys()) Out.Bones.Add(Key.Name,H->GetGlobalTransform(Key));
    Out.Switches=Switches;
    if (Out.MaximumRotationErrorDegrees>.5) {Error=FString::Printf(TEXT("Rig readback rotation %.6f degrees at %s"),Out.MaximumRotationErrorDegrees,*Out.WorstBone);return false;}
    return true;
}
}

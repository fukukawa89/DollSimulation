#include "PoseDollRigAdapter.h"
#include "Rigs/RigHierarchy.h"

namespace PoseDoll
{
bool FCuratedAdapter::Constrain(TMap<FString,FContactGoal>& Goals,FPoseResult& Pose,FString& Error)
{
    if (Goals.IsEmpty()) return true;
    if (!Rig.IsValid()) {Error=TEXT("Contact solver has no rig");return false;}
    URigHierarchy* H=Rig->GetHierarchy();
    TMap<FString,FName> Ends;
    for (auto& Pair:Goals)
    {
        const bool Arm=Pair.Key.StartsWith(TEXT("arm_"));const FString Side=Pair.Key.Right(1);
        if ((Side!=TEXT("l") && Side!=TEXT("r")) || (!Arm && !Pair.Key.StartsWith(TEXT("leg_")))) {Error=TEXT("Unknown contact chain");return false;}
        const FName A(*(FString(Arm?TEXT("upperarm_"):TEXT("thigh_"))+Side));
        const FName B(*(FString(Arm?TEXT("lowerarm_"):TEXT("calf_"))+Side));
        const FName C(*(FString(Arm?TEXT("hand_"):TEXT("foot_"))+Side));Ends.Add(Pair.Key,C);
        const FName CA(*(A.ToString()+TEXT("_fk_ctrl"))),CB(*(B.ToString()+TEXT("_fk_ctrl"))),CC(*(C.ToString()+TEXT("_fk_ctrl")));
        const FVector Root=Pose.Bones[A].GetLocation(),Middle=Pose.Bones[B].GetLocation(),End=Pose.Bones[C].GetLocation();
        const double L1=FVector::Distance(Root,Middle),L2=FVector::Distance(Middle,End);
        const FVector Difference=Pair.Value.Target.GetLocation()-Root;const double Distance=Difference.Length();
        const FVector Direction=Distance>1e-6?Difference/Distance:(End-Root).GetSafeNormal();
        const double Reach=FMath::Clamp(Distance,FMath::Abs(L1-L2)+.00001,L1+L2-.00001);
        if (FMath::Abs(Distance-Reach)>.5) Pose.bContactsReachable=false;
        FVector Pole=(Middle-Root)-Direction*FVector::DotProduct(Middle-Root,Direction);
        if (Pole.SizeSquared()<.01)
        {
            Pose.bPoleDegenerate=true;Pole=Pair.Value.PreviousPole-Direction*FVector::DotProduct(Pair.Value.PreviousPole,Direction);
            if (Pole.SizeSquared()<.01) {const FVector Preferred=FVector(0,Arm?-1:1,0);Pole=Preferred-Direction*FVector::DotProduct(Preferred,Direction);}
            if (Pole.SizeSquared()<.01) Pole=FVector::CrossProduct(Direction,FVector::XAxisVector);
        }
        Pole.Normalize();Pair.Value.PreviousPole=Pole;
        const double Along=(L1*L1-L2*L2+Reach*Reach)/(2*Reach);
        const double Height=FMath::Sqrt(FMath::Max(0.0,L1*L1-Along*Along));
        const FVector NewMiddle=Root+Direction*Along+Pole*Height,NewEnd=Root+Direction*Reach;
        FTransform Upper=Rig->GetControlGlobalTransform(CA),Lower=Rig->GetControlGlobalTransform(CB),Tip=Rig->GetControlGlobalTransform(CC);
        const FQuat UpperDelta=FQuat::FindBetweenNormals((Middle-Root).GetSafeNormal(),(NewMiddle-Root).GetSafeNormal());
        const FQuat LowerDelta=FQuat::FindBetweenNormals((End-Middle).GetSafeNormal(),(NewEnd-NewMiddle).GetSafeNormal());
        Upper.SetRotation(UpperDelta*Upper.GetRotation());
        Lower.SetLocation(NewMiddle+LowerDelta.RotateVector(Lower.GetLocation()-Middle));Lower.SetRotation(LowerDelta*Lower.GetRotation());
        Tip.SetLocation(NewEnd);
        if (Pair.Value.bLockRotation) Tip.SetRotation(Pair.Value.Target.GetRotation()*Pose.Bones[C].GetRotation().Inverse()*Tip.GetRotation());
        Rig->SetControlGlobalTransform(CA,Upper,false,Context,false);Rig->SetControlGlobalTransform(CB,Lower,false,Context,false);Rig->SetControlGlobalTransform(CC,Tip,false,Context,false);
    }
    // Analytic two-bone IK produces FK controls. The native rig owns the final bones;
    // FK/IK switches remain explicitly FK and are captured with the resulting controls.
    Rig->Evaluate_AnyThread();
    for (const auto& M:Mapping) Pose.Controls.Add(M.Control,Rig->GetControlLocalTransform(M.Control));
    for (const auto& Key:H->GetBoneKeys()) Pose.Bones.Add(Key.Name,H->GetGlobalTransform(Key));
    for (const auto& Pair:Goals)
    {
        const FTransform Actual=Pose.Bones[Ends[Pair.Key]];
        Pose.ContactPositionErrorCm=FMath::Max(Pose.ContactPositionErrorCm,FVector::Distance(Actual.GetLocation(),Pair.Value.Target.GetLocation()));
        if (Pair.Value.bLockRotation) Pose.ContactRotationErrorDegrees=FMath::Max(Pose.ContactRotationErrorDegrees,FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Pair.Value.Target.GetRotation())));
    }
    Pose.bContactsReachable &= Pose.ContactPositionErrorCm<=.5 && Pose.ContactRotationErrorDegrees<=1;
    return true;
}
}

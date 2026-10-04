#pragma once
#include "CoreMinimal.h"
#include "PoseDollRigAdapter.h"

namespace PoseDoll
{
// All solves happen on a transient rig. Only a verified result may be keyed.
struct FEditPose
{
    TMap<FName,FTransform> Controls, Bones;
    TMap<FName,bool> Switches;
    TSet<FName> RotationOnly;
    FString Warning;
};
class FNativePoseEditing
{
public:
    static FName ModeFor(const FControlMap& Map);
    static bool Capture(UControlRig* Current,const FCuratedAdapter& Adapter,const FPoseResult& Source,
        const TSet<FName>& Selected,FEditPose& Out,FString& Error);
    static bool MatchMode(UControlRig* Current,const FCuratedAdapter& Adapter,FName Mode,
        bool PreviousIK,bool NextIK,FEditPose& Out,FString& Error);
};
}

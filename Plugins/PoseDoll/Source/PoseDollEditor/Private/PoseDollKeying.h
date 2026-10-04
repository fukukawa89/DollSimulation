#pragma once
#include "CoreMinimal.h"
#include "PoseDollPoseEditing.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"

class UMovieSceneControlRigParameterTrack;

namespace PoseDoll
{
// Call Validate before Modify/transactions. Write touches only selected channels
// and matching support controls, and never rewrites another time's existing key.
class FNativePoseKeys
{
public:
    static bool MatchingEnabled(UMovieSceneControlRigParameterTrack* Track);
    static bool Validate(UMovieSceneControlRigParameterSection* Section,UControlRig* Rig,
        FFrameNumber Time,const FEditPose& Pose,FString& Error);
    static void Write(UMovieSceneControlRigParameterSection* Section,UControlRig* Rig,
        FFrameNumber Time,const FEditPose& Pose,const TMap<FName,bool>& PreviousModes,
        EMovieSceneKeyInterpolation Interpolation);
};
}

#include "PoseDollKeying.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "UObject/Package.h"

namespace PoseDoll
{
namespace
{
void WriteValue(FMovieSceneFloatChannel& Channel,FFrameNumber Time,double Value,double Before,
    bool Rotation,EMovieSceneKeyInterpolation Interpolation)
{
    const auto Times=Channel.GetData().GetTimes();
    const auto Values=Channel.GetData().GetValues();
    int32 Previous=INDEX_NONE;
    for(int32 Index=0;Index<Times.Num() && Times[Index]<Time;++Index)Previous=Index;
    if(Rotation)
    {
        const double Reference=Previous==INDEX_NONE?Before:Values[Previous].Value;
        Value=Reference+FMath::FindDeltaAngleDegrees(Reference,Value);
    }
    // The first key extrapolates backwards; keep the evaluated pose before it.
    if(Previous==INDEX_NONE && Time.Value>MIN_int32)Channel.AddConstantKey(Time-1,Before);
    if(Interpolation==EMovieSceneKeyInterpolation::Linear)Channel.AddLinearKey(Time,Value);
    else Channel.AddConstantKey(Time,Value);
}
}
bool FNativePoseKeys::MatchingEnabled(UMovieSceneControlRigParameterTrack* Track)
{
    return Track && Track->GetPackage()->GetMetaData().GetValue(Track,TEXT("PoseDoll.MatchIKFK"))==TEXT("1");
}
bool FNativePoseKeys::Validate(UMovieSceneControlRigParameterSection* Section,UControlRig* Rig,
    FFrameNumber Time,const FEditPose& Pose,FString& Error)
{
    auto Fail=[&](const FString& Message){Error=Message;return false;};
    if(!Section || !Rig || Section->IsReadOnly() || !Section->IsActive() || Rig->IsAdditive() ||
       Section->GetBlendType()!=EMovieSceneBlendType::Absolute)
        return Fail(TEXT("Capture requires one active, writable, absolute Control Rig section"));
    const auto* Warp=Section->GetTimeWarp();
    if(Warp && !(*Warp==FMovieSceneTimeWarpVariant(1.0)))return Fail(TEXT("Time warp is unsupported"));
    if(!FMath::IsNearlyEqual(Section->GetTotalWeightValue(Time),1.f))return Fail(TEXT("The Control Rig section must have full weight at the capture frame"));
    for(const auto& Pair:Pose.Controls)
    {
        const auto* Control=Rig->FindControl(Pair.Key);
        if(!Control || !Section->GetControlNameMask(Pair.Key))return Fail(TEXT("Control is missing or disabled: ")+Pair.Key.ToString());
        const bool RotationVector=Control->Settings.ControlType==ERigControlType::Rotator;
        if(RotationVector?!Section->HasVectorParameter(Pair.Key):!Section->HasTransformParameter(Pair.Key))
            return Fail(TEXT("Missing control channel: ")+Pair.Key.ToString());
        const auto Required=Pose.RotationOnly.Contains(Pair.Key)||RotationVector?
            EMovieSceneTransformChannel::Rotation:EMovieSceneTransformChannel::AllTransform;
        if(!EnumHasAllFlags(Section->GetTransformMask().GetChannels(),Required))return Fail(TEXT("Required transform channels are disabled"));
    }
    for(const auto& Pair:Pose.Switches)
        if(!Section->HasBoolParameter(Pair.Key) || !Section->GetControlNameMask(Pair.Key))return Fail(TEXT("IK/FK switch is missing or disabled: ")+Pair.Key.ToString());
    return true;
}
void FNativePoseKeys::Write(UMovieSceneControlRigParameterSection* Section,UControlRig* Rig,
    FFrameNumber Time,const FEditPose& Pose,const TMap<FName,bool>& PreviousModes,
    EMovieSceneKeyInterpolation Interpolation)
{
    if(auto* Track=Section->GetTypedOuter<UMovieSceneControlRigParameterTrack>())
        Track->GetPackage()->GetMetaData().SetValue(Track,TEXT("PoseDoll.MatchIKFK"),TEXT("1"));
    Section->ExpandToFrame(Time);
    for(const auto& Pair:Pose.Controls)
    {
        const FTransform Before=Rig->GetControlLocalTransform(Pair.Key);
        const FVector OldP=Before.GetLocation(),OldR=Before.Rotator().Euler(),OldS=Before.GetScale3D();
        const FVector P=Pair.Value.GetLocation(),R=Pair.Value.Rotator().Euler(),S=Pair.Value.GetScale3D();
        for(auto& Entry:Section->GetTransformParameterNamesAndCurves())if(Entry.ParameterName==Pair.Key)
            for(int32 Axis=0;Axis<3;++Axis)
            {
                WriteValue(Entry.Rotation[Axis],Time,R[Axis],OldR[Axis],true,Interpolation);
                if(!Pose.RotationOnly.Contains(Pair.Key))
                {
                    WriteValue(Entry.Translation[Axis],Time,P[Axis],OldP[Axis],false,Interpolation);
                    WriteValue(Entry.Scale[Axis],Time,S[Axis],OldS[Axis],false,Interpolation);
                }
            }
        for(auto& Entry:Section->GetVectorParameterNamesAndCurves())if(Entry.ParameterName==Pair.Key)
        {
            WriteValue(Entry.XCurve,Time,R.X,OldR.X,true,Interpolation);
            WriteValue(Entry.YCurve,Time,R.Y,OldR.Y,true,Interpolation);
            WriteValue(Entry.ZCurve,Time,R.Z,OldR.Z,true,Interpolation);
        }
    }
    for(const auto& Pair:Pose.Switches)
    {
        bool HasEarlier=false;
        for(const auto& Entry:Section->GetBoolParameterNamesAndCurves())if(Entry.ParameterName==Pair.Key)
            for(FFrameNumber Key:Entry.ParameterCurve.GetData().GetTimes())if(Key<Time)HasEarlier=true;
        const bool* Before=PreviousModes.Find(Pair.Key);
        if(!HasEarlier && Before && Time.Value>MIN_int32)Section->AddBoolParameterKey(Pair.Key,Time-1,*Before);
        Section->AddBoolParameterKey(Pair.Key,Time,Pair.Value);
    }
    Section->MarkAsChanged();
}
}

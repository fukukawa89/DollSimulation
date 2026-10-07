#include "PoseDollSession.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieScene.h"
#include "Misc/DateTime.h"

namespace PoseDoll
{
void FSession::CancelSnapshot(const FString& Reason)
{
    if(bStaticCommit)return;
    if(StaticWindow.Pending()||bSnapshotEligible)
    {
        if(Input)Input->StaticRequest(StaticWindow.CaptureId,TEXT("cancel"));
        StaticWindow.Cancel();bSnapshotEligible=false;State=TEXT("SnapshotCancelled");Error=Reason;
    }
}
void FSession::InvalidateSnapshotContext(){++ContextRevision;CancelSnapshot(TEXT("Editor context changed; request another snapshot"));}
void FSession::ObserveObjectModified(UObject* Object)
{
    if(bStaticCommit||!Sequence.IsValid()||!Object)return;
    if(Object==Sequence.Get()||Object->IsIn(Sequence.Get())||Object==Component.Get()||(Component.IsValid()&&Object==Component->GetOwner()))InvalidateSnapshotContext();
}
bool FSession::StaticContextMatches() const
{
    if(ContextRevision!=SnapshotRevision||Sequence!=SnapshotSequence||Component!=SnapshotComponent||Binding!=SnapshotBinding||Mask!=SnapshotMask||Profile.Hash!=SnapshotProfile||Profile.CalibrationHash!=SnapshotCalibration)return false;
    if(Sequence.IsValid())
    {
        const auto Position=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame;
        if(ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence()!=Sequence.Get()||ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence()!=Sequence.Get()||Position.FrameNumber.Value!=SnapshotFrame||Position.GetSubFrame()!=SnapshotSubFrame)return false;
    }
    return true;
}
bool FSession::RequestSnapshot(bool WriteKeys,int32 Advance,bool Linear)
{
    if(!Initialize()||!Input){Error=TEXT("Connect a PDS1 static source first");return false;}
    const auto S=Input->Snapshot();
    if(!S.bConnected||!S.bStatic){Error=TEXT("This source does not support static requests");return false;}
    if(CommittedSnapshots.Num()>=4096){Error=TEXT("Static session limit reached; reconnect before capturing");return false;}
    if(StaticWindow.Pending())return true; // A second click is idempotent, not another key.
    if(WriteKeys&&!ValidateTarget(true))return false;
    if(Sequence.IsValid()&&!ValidateTarget(true))return false;
    CancelSnapshot(TEXT("New capture request"));bFixture=false;bSnapshotEligible=false;
    SnapshotSequence=Sequence;SnapshotComponent=Component;SnapshotBinding=Binding;SnapshotMask=Mask;SnapshotProfile=Profile.Hash;SnapshotCalibration=Profile.CalibrationHash;SnapshotRevision=ContextRevision;
    const auto Position=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame;
    SnapshotFrame=Position.FrameNumber.Value;SnapshotSubFrame=Position.GetSubFrame();SnapshotAdvance=Advance;bSnapshotLinear=Linear;bSnapshotWrite=WriteKeys;
    Generation=S.Generation;SnapshotSourceKind=S.StaticIdentity.SourceKind;
    StaticWindow.Begin(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),FPlatformTime::Seconds());
    if(!Input->StaticRequest(StaticWindow.CaptureId,TEXT("request"))){StaticWindow.Cancel();Error=TEXT("Static command queue unavailable");return false;}
    State=TEXT("SnapshotRequested");Error.Empty();return true;
}
bool FSession::TickStatic(const FTransportSnapshot& S,double Now)
{
    if((StaticWindow.Pending()||bSnapshotEligible)&&(!StaticContextMatches()||(Sequence.IsValid()&&!ValidateTarget(true))))CancelSnapshot(TEXT("Target, timeline or editing context changed"));
    if(!StaticWindow.CheckDeadline(Now,Error)){State=TEXT("SnapshotTimedOut");Input->StaticRequest(StaticWindow.CaptureId,TEXT("cancel"));}
    for(const auto& M:Input->DrainStatic())
    {
        if(!StaticWindow.Pending())continue;
        if(!StaticWindow.Push(Profile,M,Error)){State=StaticWindow.State;Input->StaticRequest(StaticWindow.CaptureId,TEXT("cancel"));continue;}
        State=TEXT("Snapshot")+StaticWindow.State;
        if(StaticWindow.State==TEXT("SnapshotReady"))
        {
            // Include time spent in the game-thread queue in the age/deadline check.
            const double AgeUs=(Now-StaticWindow.Started)*1e6-double(M.Start-M.RequestStart);
            if(Now-StaticWindow.Started>3||AgeUs>200000||!StaticContextMatches()){StaticWindow.Cancel();State=TEXT("SnapshotRejected");Error=TEXT("Static result expired or context changed before application");continue;}
            const auto OldQ=Q;const auto OldSource=SourcePose;const auto OldRaw=RawPose;const auto OldPose=Pose;
            TGuardValue<bool> StaticApplyGuard(bApplyingStatic,true);
            if(!Apply(StaticWindow.Final.Sample,S.Identity)){Q=OldQ;SourcePose=OldSource;RawPose=OldRaw;Pose=OldPose;StaticWindow.Cancel();continue;}
            HistoricalSnapshot=StaticWindow.Final;HistoricalReceived=Now;bHasSnapshot=true;bSnapshotEligible=true;SnapshotCapturedUtc=FDateTime::UtcNow().ToIso8601();State=TEXT("SnapshotReady");
            Input->StaticRequest(StaticWindow.CaptureId,TEXT("ack"));
            if(bSnapshotWrite&&!Capture(SnapshotFrame,SnapshotAdvance,bSnapshotLinear))State=TEXT("SnapshotCaptureFailed");
        }
    }
    return true;
}
FString FSession::SnapshotLabel() const
{
    FString Text=TEXT("静态采集：")+StaticWindow.State;
    if(bHasSnapshot)Text+=TEXT("  ·  确认于 ")+SnapshotCapturedUtc+FString::Printf(TEXT("（%.1f 秒前）"),FPlatformTime::Seconds()-HistoricalReceived);
    return Text;
}
}

#pragma once
#include "CoreMinimal.h"
#include "PoseDollCore.h"
#include "PoseDollTransport.h"
#include "PoseDollRigAdapter.h"
#include "LevelSequence.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"

namespace PoseDoll
{
class FSession
{
public:
    static FSession& Get();
    bool Initialize();
    void Shutdown();
    bool Tick(float DeltaSeconds);
    bool Connect(uint16 Port=39177);
    void Disconnect();
    bool Resume(bool Clutch=false);
    void Freeze();
    void SetMask(const FString& InMask);
    bool LoadFixture(const FString& Filename);
    bool Bind(ULevelSequence* Sequence,USkeletalMeshComponent* Component);
    bool BindSelection();
    bool Capture(int32 Frame,int32 Advance=0,bool Linear=false);
    bool RequestSnapshot(bool WriteKeys=false,int32 Advance=0,bool Linear=false,bool Clutch=false);
    void CancelSnapshot(const FString& Reason=TEXT("Cancelled by user"));
    void InvalidateSnapshotContext();
    void AfterUndoRedo();
    void ObserveObjectModified(UObject* Object);
    FString SnapshotLabel() const;
    FStaticWindow StaticWindow;
    bool Contact(const FString& Chain,bool Enabled,bool LockRotation=true,const FVector* GoalPosition=nullptr);
    FString StatusJson() const;
    FString DiagnosticText() const;
    FString PoseReport() const;
    FString KeyReport() const;
    FString State=TEXT("Disconnected"),Error,Mask=TEXT("FullBody");
    FProfile Profile;
    TUniquePtr<FCuratedAdapter> Adapter;
    FPoseResult Pose,RawPose;
    TMap<FString,FContactGoal> Contacts;
    TArray<FMatrix44> SourcePose;
    TArray<double> Q;
    FTransform Placement=FTransform::Identity;
    FTransform BoundActorTransform=FTransform::Identity;
    TWeakObjectPtr<ULevelSequence> Sequence;
    TWeakObjectPtr<USkeletalMeshComponent> Component;
    TWeakObjectPtr<UMovieSceneControlRigParameterTrack> Track;
    FGuid Binding;
    uint64 Applied=0,Invalid=0,Captures=0;
    TArray<double> ProcessingTimes,ReceiveLatency;
    TArray<double> MainThreadTimes,PreviewLatency;
    double LastProcessingMs=0,LastReceivedSeconds=0;
    uint64 PreviewFrames=0;
    void RecordPreview(double DurationMs);
    bool IsMasked(const FControlMap& M) const;
private:
    bool Apply(const FSample& Sample,const FIdentity& Identity);
    bool StaticContextMatches() const;
    bool TickStatic(const FTransportSnapshot& Transport,double Now);
    bool bRigNeedsRebuild=false;
    bool bApplyingStatic=false,bSnapshotEligible=false,bStaticCommit=false,bSnapshotWrite=false,bSnapshotLinear=false;
    int32 SnapshotFrame=0,SnapshotAdvance=0;float SnapshotSubFrame=0;
    uint64 ContextRevision=0,SnapshotRevision=0;
    TWeakObjectPtr<ULevelSequence> SnapshotSequence;
    TWeakObjectPtr<USkeletalMeshComponent> SnapshotComponent;
    FGuid SnapshotBinding;
    FString SnapshotMask,SnapshotProfile,SnapshotCalibration,SnapshotCapturedUtc,SnapshotSourceKind;
    FTransform SnapshotPlacement;
    FStaticMessage HistoricalSnapshot;
    bool bHasSnapshot=false;
    double HistoricalReceived=0;
    TSet<FString> CommittedSnapshots;
    bool ValidateTarget(bool ForCapture);
    void ApplyMode();
    void SnapshotBaseline();
    TUniquePtr<FTcpSource> Input;
    FDecoder Decoder;
    uint64 Generation=0,LastSequence=MAX_uint64;
    double LastValidSeconds=0;
    bool bLive=false,bValid=false,bFixture=false,bClutch=false;
    int32 ClutchFrame=0;
    FPoseResult SourceBaseline,TargetBaseline;
    FIdentity LastIdentity;
    FSample LastSample;
    FSample DiagnosticSample;
    TWeakObjectPtr<UObject> RigAsset;
    FDelegateHandle RigCompiledHandle;
};
}

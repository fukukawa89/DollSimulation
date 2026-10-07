#pragma once
#include "CoreMinimal.h"
#include "PoseDollCore.h"
#include "PoseDollTransport.h"
#include "PoseDollRigAdapter.h"
#include "LevelSequence.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"

namespace PoseDoll
{
enum class EHandSide : uint8;
class FSession
{
public:
    static FSession& Get();
    bool Initialize();
    void Shutdown();
    bool Tick(float DeltaSeconds);
    bool Connect(uint16 Port=39177);
    void Disconnect();
    bool CaptureCurrent(int32 Advance=0,bool Linear=false);
    bool ApplyHandPreset(const FString& Id,EHandSide Side,bool Linear=false);
    bool SetCustomParts(const TSet<FString>& Parts);
    TSet<FString> GetSelectedParts() const;
    void TogglePart(const FString& Part,bool Enabled);
    static const TArray<TPair<FString,FString>>& PartOptions();
    static FString MaskLabel(const FString& Value);
    FString SelectionLabel() const;
    TSet<FString> CustomParts;
    void TickRigEditing();
    void ClearRigObservers();
    void SetMask(const FString& InMask);
    bool LoadFixture(const FString& Filename);
    bool Bind(ULevelSequence* Sequence,USkeletalMeshComponent* Component);
    bool BindSelection();
    bool Capture(int32 Frame,int32 Advance=0,bool Linear=false);
    bool RequestSnapshot(bool WriteKeys=false,int32 Advance=0,bool Linear=false);
    void CancelSnapshot(const FString& Reason);
    void InvalidateSnapshotContext();
    void AfterUndoRedo();
    void ObserveObjectModified(UObject* Object);
    FString SnapshotLabel() const;
    FString SourceLabel() const;
    FString UserStatusLabel() const;
    FStaticWindow StaticWindow;
    FString StatusJson() const;
    FString DiagnosticText() const;
    FString PoseReport() const;
    FString KeyReport() const;
    FString State=TEXT("Disconnected"),Error,EditingNote,Mask=TEXT("FullBody");
    FProfile Profile;
    TUniquePtr<FCuratedAdapter> Adapter;
    FPoseResult Pose,RawPose;
    TArray<FMatrix44> SourcePose;
    TArray<double> Q;
    TWeakObjectPtr<ULevelSequence> Sequence;
    TWeakObjectPtr<USkeletalMeshComponent> Component;
    TWeakObjectPtr<UMovieSceneControlRigParameterTrack> Track;
    FGuid Binding;
    uint64 Applied=0,Invalid=0,Captures=0,PreviewRevision=0;
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
    struct FRigObserver
    {
        FDelegateHandle Modified,Evaluated;
        TMap<FName,bool> Modes;
    };
    TMap<TWeakObjectPtr<UControlRig>,FRigObserver> RigObservers;
    bool bEditingRig=false;
    void RememberRigModes(UControlRig* Rig);
    void OnRigModified(UControlRig* Rig,FRigControlElement* Control,const FRigControlModifiedContext& Context);
    bool bRigNeedsRebuild=false;
    bool bApplyingStatic=false,bSnapshotEligible=false,bStaticCommit=false,bSnapshotWrite=false,bSnapshotLinear=false;
    int32 SnapshotFrame=0,SnapshotAdvance=0;float SnapshotSubFrame=0;
    uint64 ContextRevision=0,SnapshotRevision=0;
    TWeakObjectPtr<ULevelSequence> SnapshotSequence;
    TWeakObjectPtr<USkeletalMeshComponent> SnapshotComponent;
    FGuid SnapshotBinding;
    FString SnapshotMask,SnapshotProfile,SnapshotCalibration,SnapshotCapturedUtc,SnapshotSourceKind;
    FStaticMessage HistoricalSnapshot;
    bool bHasSnapshot=false;
    double HistoricalReceived=0;
    TSet<FString> CommittedSnapshots;
    bool ValidateTarget(bool ForCapture);
    TUniquePtr<FTcpSource> Input;
    FDecoder Decoder;
    uint64 Generation=0;
    double LastValidSeconds=0;
    bool bValid=false,bFixture=false;
    FIdentity LastIdentity;
    FSample LastSample;
    FSample DiagnosticSample;
    TWeakObjectPtr<UObject> RigAsset;
    FDelegateHandle RigCompiledHandle;
};
}

#include "PoseDollSession.h"
#include "PoseDollPoseEditing.h"
#include "PoseDollKeying.h"
#include "PoseDollEditorLibrary.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSequencePlayer.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "ScopedTransaction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

namespace PoseDoll
{
static TSharedRef<FJsonObject> TransformData(const FTransform& T)
{
    auto O=MakeShared<FJsonObject>();const auto P=T.GetLocation(),S=T.GetScale3D();const auto Q=T.GetRotation();
    auto Array=[](std::initializer_list<double> Values){TArray<TSharedPtr<FJsonValue>> A;for(double V:Values) A.Add(MakeShared<FJsonValueNumber>(V));return A;};
    O->SetArrayField(TEXT("translation_cm"),Array({P.X,P.Y,P.Z}));O->SetArrayField(TEXT("rotation_xyzw"),Array({Q.X,Q.Y,Q.Z,Q.W}));O->SetArrayField(TEXT("scale"),Array({S.X,S.Y,S.Z}));return O;
}
static TSharedRef<FJsonObject> ControlData(const TMap<FName,FTransform>& Controls)
{auto O=MakeShared<FJsonObject>();for(const auto& P:Controls) O->SetObjectField(P.Key.ToString(),TransformData(P.Value));return O;}
FSession& FSession::Get() {static FSession Instance;return Instance;}
bool FSession::Initialize()
{
    if (Adapter) return true;
    const FString Root=FPaths::ProjectDir()/TEXT("Shared/Profiles");TSharedPtr<FJsonObject> Target;
    if (!Profile.Load(Root,Error) || !LoadJson(Root/TEXT("manny_body_ue582_v1.json"),Target,Error)) return false;
    auto* Asset=LoadObject<UControlRigBlueprint>(nullptr,*Target->GetStringField(TEXT("rig")));
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*Target->GetStringField(TEXT("mesh")));
    auto Candidate=MakeUnique<FCuratedAdapter>();
    if (!Asset || !Candidate->Initialize(Asset->GeneratedClass,Mesh,Root/TEXT("manny_body_ue582_v1.json"),Error)) {State=TEXT("Fault");return false;}
    Adapter=MoveTemp(Candidate);
    RigAsset=Asset;
    // VM-only recompiles do not reliably replace the generated class or emit OnObjectModified.
    RigCompiledHandle=Asset->OnVMCompiled().AddLambda([this](UObject*,URigVM*,FRigVMExtendedExecuteContext&)
    {
        Shutdown();State=TEXT("Fault");Error=TEXT("Target Rig VM was recompiled; rebind and validate before capturing");
    });
    return true;
}
void FSession::Shutdown()
{
    ClearRigObservers();
    if (auto* Asset=Cast<UControlRigBlueprint>(RigAsset.Get())) Asset->OnVMCompiled().Remove(RigCompiledHandle);
    RigCompiledHandle.Reset();RigAsset.Reset();
    Disconnect();Adapter.Reset();Sequence.Reset();Component.Reset();Track.Reset();Binding.Invalidate();
}
bool FSession::Connect(uint16 Port)
{
    if (!Initialize()) return false;
    Disconnect();Input=MakeUnique<FTcpSource>(Profile);Input->Start(Port);State=TEXT("Connecting");Error.Empty();return true;
}
void FSession::Disconnect()
{
    CancelSnapshot(TEXT("Disconnected"));CommittedSnapshots.Reset();bValid=false;bFixture=false;Input.Reset();Decoder.Reset();Generation=0;State=TEXT("Disconnected");
}
void FSession::SetMask(const FString& InMask) {InvalidateSnapshotContext();Mask=InMask;bValid=false;Error.Empty();}
bool FSession::IsMasked(const FControlMap& M) const
{
    return Mask==TEXT("FullBody") || (Mask==TEXT("Custom") && CustomParts.Contains(M.Semantic)) ||
        (Mask==TEXT("UpperBody") && !M.Group.StartsWith(TEXT("leg")) && M.Group!=TEXT("pelvis")) ||
        (Mask==TEXT("LowerBody") && M.Group.StartsWith(TEXT("leg"))) || Mask==M.Group;
}
bool FSession::CaptureCurrent(int32 Advance,bool Linear)
{
    if(!Initialize() || !ValidateTarget(true))return false;
    if(ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame.GetSubFrame()!=0)
    {Error=TEXT("Choose a whole display frame before capturing");return false;}
    TSet<FName> Selected;for(const auto& M:Adapter->GetMapping())if(IsMasked(M))Selected.Add(M.Control);
    if(Selected.IsEmpty()){Error=TEXT("Select at least one capture part");return false;}
    if(Input && Input->Snapshot().bStatic)return RequestSnapshot(true,Advance,Linear);
    const int32 Frame=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame.FrameNumber.Value;
    if(bFixture)return Capture(Frame,Advance,Linear);
    if(!Input){Error=TEXT("Connect a source before capturing");return false;}
    const auto Source=Input->Snapshot();
    if(!Source.bConnected || !Source.bHasSample || FPlatformTime::Seconds()-Source.Latest.ReceivedSeconds>.25)
    {Error=TEXT("Capture requires a fresh complete sample");return false;}
    if(!Apply(Source.Latest,Source.Identity))return false;
    return Capture(Frame,Advance,Linear);
}
bool FSession::Apply(const FSample& Sample,const FIdentity& Identity)
{
    const double Begin=FPlatformTime::Seconds();
    DiagnosticSample=Sample;
    FStaticMessage AbsoluteMessage;AbsoluteMessage.Sample=Sample;
    if (!(bApplyingStatic?DecodeAbsolute(Profile,AbsoluteMessage,Q,Error):Decoder.Decode(Profile,Identity,Sample,Q,Error)) || !Profile.Forward(Q,SourcePose,Error) || !Adapter->Apply(Profile,SourcePose,RawPose,Error)) {++Invalid;bValid=false;State=TEXT("Stale");return false;}
    Pose=RawPose;++PreviewRevision;
    LastSample=Sample;LastIdentity=Identity;bValid=true;LastValidSeconds=FPlatformTime::Seconds();++Applied;
    LastProcessingMs=(LastValidSeconds-Begin)*1000;LastReceivedSeconds=Sample.ReceivedSeconds;ProcessingTimes.Add(LastProcessingMs);ReceiveLatency.Add((LastValidSeconds-Sample.ReceivedSeconds)*1000);
    // Keep diagnostics bounded across repeated one-shot captures.
    if (ProcessingTimes.Num()>3600) {ProcessingTimes.RemoveAt(0,600,EAllowShrinking::No);ReceiveLatency.RemoveAt(0,600,EAllowShrinking::No);}
    return true;
}
bool FSession::Tick(float)
{
    TickRigEditing();
    if (!Input) return true;
    const auto S=Input->Snapshot();const double Now=FPlatformTime::Seconds();
    if (S.Generation!=Generation) {CancelSnapshot(TEXT("Source session changed"));Generation=S.Generation;Decoder.Reset();bValid=false;State=TEXT("Ready");}
    if (!S.bConnected) {CancelSnapshot(TEXT("Source disconnected"));bValid=false;State=S.State;Error=S.Error;return true;}
    if(S.bStatic)return TickStatic(S,Now);
    // Streaming simulator samples remain buffered until an explicit capture.
    return true;
}
bool FSession::LoadFixture(const FString& Filename)
{
    if (FPaths::GetCleanFilename(Filename)!=Filename || !Filename.EndsWith(TEXT(".sample.json"))) {Error=TEXT("Fixture must be a bundled sample filename");return false;}
    if (!Initialize()) return false;
    TSharedPtr<FJsonObject> Hello,Raw;FIdentity ID;FSample Sample;
    const FString Root=FPaths::ProjectDir()/TEXT("Shared/Fixtures");
    if (!LoadJson(Root/TEXT("hello.json"),Hello,Error) || !Handshake(Profile,*Hello,ID,Error) || !LoadJson(Root/Filename,Raw,Error) || !ParseSample(Profile,ID,*Raw,Sample,Error)) return false;
    // A fixture is an explicit source switch, not an overlay on a live transport.
    Disconnect();Sample.ReceivedSeconds=FPlatformTime::Seconds();
    if (!Apply(Sample,ID)) return false;
    bFixture=true;State=TEXT("PoseReady");Error.Empty();return true;
}
bool FSession::ValidateTarget(bool ForCapture)
{
    if (!Sequence.IsValid() || !Component.IsValid() || !Component->GetOwner() || Component->GetWorld()!=GEditor->GetEditorWorldContext().World()) {Error=TEXT("Target actor/component/sequence is gone or belongs to another world");return false;}
    if (!Sequence->GetMovieScene() || Sequence->GetMovieScene()->IsReadOnly()) {Error=TEXT("The sequence is read-only");return false;}
    if (ForCapture && ULevelSequenceEditorBlueprintLibrary::IsPlaying()) {Error=TEXT("Pause Sequencer before capturing a pose");return false;}
    const FMovieSceneBinding* Bound=Sequence->GetMovieScene()->FindBinding(Binding);
    if(!Bound){Error=TEXT("The target binding no longer exists");return false;}
    int32 RigCount=0;
    for(auto* Base:Bound->GetTracks())
    {
        if(auto* CR=Cast<UMovieSceneControlRigParameterTrack>(Base))
        {
            ++RigCount;
            if(RigCount>1 || (Track.IsValid() && Track.Get()!=CR) || !CR->GetControlRig() || CR->GetControlRig()->IsAdditive() || CR->GetControlRig()->GetClass()!=Adapter->GetRig()->GetClass())
            {Error=TEXT("Capture requires a single compatible Control Rig track");return false;}
        }
        else if(Base->GetClass()->GetName().Contains(TEXT("SkeletalAnimation")))
        {Error=TEXT("Remove or bake the competing skeletal animation track before capturing");return false;}
    }
    if (Component->GetSkeletalMeshAsset()!=Adapter->GetMesh()) {Error=TEXT("This profile supports the validated SKM_Manny_Simple only");return false;}
    const FVector S=Component->GetComponentTransform().GetScale3D();
    if (S.GetMin()<=0 || !FMath::IsNearlyEqual(S.X,S.Y,1e-5) || !FMath::IsNearlyEqual(S.X,S.Z,1e-5)) {Error=TEXT("Nonuniform or negative component scale is unsupported");return false;}
    const FGuid Actual=Sequence->FindBindingFromObject(Component.Get(),Component->GetWorld());
    const FGuid ActorBinding=Sequence->FindBindingFromObject(Component->GetOwner(),Component->GetWorld());
    if (Binding!=Actual && Binding!=ActorBinding) {Error=TEXT("The selected binding no longer resolves to this component");return false;}
    if (ForCapture && (ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence()!=Sequence || ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence()!=Sequence)) {Error=TEXT("Open the bound top-level sequence; nested/focused subsequences are unsupported");return false;}
    if (Track.IsValid() && (!Track->GetControlRig() || Track->GetControlRig()->GetClass()!=Adapter->GetRig()->GetClass())) {Error=TEXT("Rig class changed; rebind and recalibrate");return false;}
    if (Track.IsValid() && !Sequence->GetMovieScene()->FindBinding(Binding)->GetTracks().Contains(Track.Get())) {Error=TEXT("Track changed or was undone; rebind the target");return false;}
    return true;
}
bool FSession::Bind(ULevelSequence* InSequence,USkeletalMeshComponent* InComponent)
{
    if (!Initialize()) return false;
    InvalidateSnapshotContext();Sequence=InSequence;Component=InComponent;Track.Reset();Binding.Invalidate();bValid=false;
    if (!InSequence || !InComponent || !InComponent->GetOwner()) {Error=TEXT("Select one actor component and a Level Sequence");return false;}
    Binding=InSequence->FindBindingFromObject(InComponent,InComponent->GetWorld());
    if (!Binding.IsValid()) Binding=InSequence->FindBindingFromObject(InComponent->GetOwner(),InComponent->GetWorld());
    if (!Binding.IsValid() || !ValidateTarget(false)) {Error=TEXT("Add this actor to the sequence first. ")+Error;return false;}
    const FMovieSceneBinding* Bound=InSequence->GetMovieScene()->FindBinding(Binding);
    for (UMovieSceneTrack* T:Bound->GetTracks())
    {
        if (auto* CR=Cast<UMovieSceneControlRigParameterTrack>(T))
        {
            if (Track.IsValid() || !CR->GetControlRig() || CR->GetControlRig()->GetClass()!=Adapter->GetRig()->GetClass() || CR->GetControlRig()->IsAdditive())
            {Error=TEXT("Select one compatible, non-layered Manny Control Rig track");return false;}
            Track=CR;
        }
        else if (T->GetClass()->GetName().Contains(TEXT("SkeletalAnimation"))) {Error=TEXT("Binding has an animation track; select an isolated PoseDoll target");return false;}
    }
    bRigNeedsRebuild=false;TickRigEditing();
    Error.Empty();return true;
}
void FSession::AfterUndoRedo()
{
    ClearRigObservers();bRigNeedsRebuild=true;InvalidateSnapshotContext();
    if (!Sequence.IsValid())return;
    const FMovieSceneBinding* Bound=Sequence->GetMovieScene()->FindBinding(Binding);
    Track.Reset();
    if (Bound)for(auto* T:Bound->GetTracks())
        if(auto* CR=Cast<UMovieSceneControlRigParameterTrack>(T))
            if(CR->GetControlRig() && Adapter && CR->GetControlRig()->GetClass()==Adapter->GetRig()->GetClass())Track=CR;
}
bool FSession::BindSelection()
{
    auto* Seq=ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
    if (GEditor->GetSelectedActorCount()!=1) {Error=TEXT("Select exactly one Manny actor");return false;}
    AActor* Actor=Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
    TArray<USkeletalMeshComponent*> Components;if (Actor) Actor->GetComponents(Components);
    if (Components.Num()!=1) {Error=TEXT("Select a target with exactly one skeletal mesh component");return false;}
    return Bind(Seq,Components[0]);
}
bool FSession::Capture(int32 Frame,int32 Advance,bool Linear)
{
    if(StaticWindow.Pending()){Error=TEXT("Wait for the current static request");return false;}
    if(!bFixture&&Input&&Input->Snapshot().bStatic&&!bSnapshotEligible){Error=TEXT("Request a new complete snapshot before Capture");return false;}
    const bool StaticCapture=bSnapshotEligible;
    if(StaticCapture && SnapshotSubFrame!=0){Error=TEXT("Choose a whole display frame before requesting a capture");return false;}
    if(StaticCapture && (CommittedSnapshots.Contains(HistoricalSnapshot.CaptureId)||!StaticContextMatches()||Frame!=SnapshotFrame)) {Error=TEXT("Snapshot already committed or capture context changed; request again");return false;}
    if (!bValid || (!bFixture && !StaticCapture && (FPlatformTime::Seconds()-LastValidSeconds>.25))) {Error=TEXT("Capture requires a fresh, complete, valid pose");return false;}
    if (!ValidateTarget(true)) return false;
    if(GEditor->IsTransactionActive()){Error=TEXT("Finish the current edit before capturing a pose");return false;}
    TGuardValue<bool> StaticCommitGuard(bStaticCommit,true);
    TGuardValue<bool> RigEditGuard(bEditingRig,true);
    FPoseResult CapturedPose=Pose;FEditPose Edit;TMap<FName,bool> PreviousModes;UMovieScene* Movie=Sequence->GetMovieScene();
    // Joint capture never authors actor placement, including existing legacy tracks.
    const FFrameNumber Time=FFrameRate::TransformTime(FFrameTime(Frame),Movie->GetDisplayRate(),Movie->GetTickResolution()).RoundToFrame();
    if (Track.IsValid())
    {
        if (Track->GetAllSections().Num()!=1) {Error=TEXT("Exactly one managed section is required");return false;}
        const auto* Warp=Track->GetAllSections()[0]->GetTimeWarp();
        if (Warp && !(*Warp==FMovieSceneTimeWarpVariant(1.0))) {Error=TEXT("Time warp is unsupported");return false;}
    }
    bool Success=true;FString Failure;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("PoseDoll Capture")));Sequence->Modify();Movie->Modify();
        if (!Track.IsValid())
        {
            const FMovieSceneBindingProxy Proxy(Binding,Sequence.Get());
            Track=Cast<UMovieSceneControlRigParameterTrack>(UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(Component->GetWorld(),Sequence.Get(),Adapter->GetRig()->GetClass(),Proxy));
            if (Track.IsValid()) {Track->Modify();Track->SetDisplayName(FText::FromString(TEXT("PoseDoll / Manny")));}
        }
        if (bRigNeedsRebuild && Track.IsValid())
        {
            // The managed instance restored by track Undo/Redo can retain stale hierarchy
            // caches. Replace only its runtime; section keys and masks stay intact.
            Track->Modify();for(auto* S:Track->GetAllSections())S->Modify();
            auto* Fresh=NewObject<UControlRig>(Track.Get(),Adapter->GetRig()->GetClass(),NAME_None,RF_Transactional);
            Fresh->SetObjectBinding(Track->GetControlRig()->GetObjectBinding());
            Fresh->Initialize();Fresh->RequestConstruction();Fresh->Evaluate_AnyThread();
            Track->ReplaceControlRig(Fresh,false);bRigNeedsRebuild=false;
        }
        auto* Section=Track.IsValid() && Track->GetAllSections().Num()==1?Cast<UMovieSceneControlRigParameterSection>(Track->GetAllSections()[0]):nullptr;
        if (!Section || Section->IsReadOnly() || !Section->IsActive() || Track->GetControlRig()->IsAdditive())
        {Success=false;Failure=TEXT("Capture requires one active, writable, non-layered Control Rig section");}
        if(Success)
        {
            ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame),EUpdatePositionMethod::Jump));
            Track->GetControlRig()->Evaluate_AnyThread();
            for(const auto& M:Adapter->GetMapping())
            {
                const FName Mode=FNativePoseEditing::ModeFor(M);
                if(!Mode.IsNone())PreviousModes.Add(Mode,Track->GetControlRig()->GetControlValue(Mode).Get<bool>());
            }
            TSet<FName> Selected;for(const auto& M:Adapter->GetMapping())if(IsMasked(M))Selected.Add(M.Control);
            Success=FNativePoseEditing::Capture(Track->GetControlRig(),*Adapter,RawPose,Selected,Edit,Failure);
            if(Success){CapturedPose.Controls=Edit.Controls;CapturedPose.Switches=Edit.Switches;CapturedPose.Bones=Edit.Bones;}
        }
        if(Success)Success=FNativePoseKeys::Validate(Section,Track->GetControlRig(),Time,Edit,Failure);
        if (Success)
        {
            Track->Modify();Section->Modify();
            const auto Interp=Linear?EMovieSceneKeyInterpolation::Linear:EMovieSceneKeyInterpolation::Constant;
            TSet<FName> Names;for(const auto& Pair:CapturedPose.Controls)Names.Add(Pair.Key);
            FNativePoseKeys::Write(Section,Track->GetControlRig(),Time,Edit,PreviousModes,Interp);
            Section->MarkAsChanged();ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
            ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame),EUpdatePositionMethod::Jump));
            UControlRig* Target=Track->GetControlRig();
            for (const auto& Pair:CapturedPose.Switches)
            {
                if (Target->GetControlValue(Pair.Key).Get<bool>()!=Pair.Value)
                {Success=false;Failure=TEXT("Sequence switch readback failed: ")+Pair.Key.ToString();break;}
            }
            for (FName Name:Names)
            {
                // Read the actual sequencer-owned rig after forced evaluation; do not create
                // a second playback player just to interrogate controls.
                const FTransform Read=Target->GetControlLocalTransform(Name);
                if (!Read.Equals(CapturedPose.Controls[Name],1e-3)) {Success=false;Failure=TEXT("Sequence control readback failed: ")+Name.ToString();break;}
            }
            ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame),EUpdatePositionMethod::Jump));
            Target->Evaluate_AnyThread();
            for (const auto& M:Adapter->GetMapping()) if (Success)
            {
                const FTransform Read=Target->GetHierarchy()->GetGlobalTransform(FRigElementKey(M.Bone,ERigElementType::Bone));
                const FTransform Expected=CapturedPose.Bones[M.Bone];
                const double RotationError=FMath::RadiansToDegrees(Read.GetRotation().AngularDistance(Expected.GetRotation()));
                const double PositionError=FVector::Distance(Read.GetLocation(),Expected.GetLocation());
                if (RotationError>.5 || PositionError>.1) {UE_LOG(LogTemp, Error,TEXT("PoseDoll %s actual=%s expected=%s"),*M.Bone.ToString(),*Read.ToString(),*Expected.ToString());Success=false;Failure=FString::Printf(TEXT("Bone readback %s: %.4f deg, %.4f cm"),*M.Bone.ToString(),RotationError,PositionError);}
            }
        }
    }
    if (!Success)
    {
        GEditor->UndoTransaction();Error=TEXT("Capture rolled back: ")+Failure;State=TEXT("Fault");Track.Reset();
        for (const auto& P:UControlRigSequencerEditorLibrary::GetControlRigs(Sequence.Get())) if (P.Proxy.BindingID==Binding && P.ControlRig && P.ControlRig->GetClass()==Adapter->GetRig()->GetClass()) Track=P.Track;
        return false;
    }
    Pose=CapturedPose;++PreviewRevision;TickRigEditing();
    EditingNote.Empty();++Captures;State=StaticCapture?TEXT("SnapshotCommitted"):TEXT("Captured");Error.Empty();
    if(StaticCapture){CommittedSnapshots.Add(HistoricalSnapshot.CaptureId);bSnapshotEligible=false;StaticWindow.State=TEXT("Committed");}
    if (Advance) ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame+Advance),EUpdatePositionMethod::Jump));
    RememberRigModes(Track->GetControlRig());
    auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("profile_sha256"),Profile.Hash);Record->SetStringField(TEXT("calibration_sha256"),Profile.CalibrationHash);Record->SetStringField(TEXT("target_profile"),Adapter->ProfileId);Record->SetStringField(TEXT("sequence"),Sequence->GetPathName());Record->SetStringField(TEXT("binding"),Binding.ToString());Record->SetStringField(TEXT("mask"),Mask);Record->SetStringField(TEXT("mode"),TEXT("ReplaceLocalRotation"));
    TArray<TSharedPtr<FJsonValue>> CapturedParts;for(const FString& Part:CustomParts)CapturedParts.Add(MakeShared<FJsonValueString>(Part));Record->SetArrayField(TEXT("custom_parts"),CapturedParts);Record->SetNumberField(TEXT("display_frame"),Frame);
    if(StaticCapture)
    {
        Record->SetStringField(TEXT("input_mode"),TEXT("PDS1/1 Snapshot"));Record->SetStringField(TEXT("capture_id"),HistoricalSnapshot.CaptureId);Record->SetStringField(TEXT("captured_utc"),SnapshotCapturedUtc);Record->SetStringField(TEXT("source_kind"),SnapshotSourceKind);
        Record->SetStringField(TEXT("source_start_us"),FString::Printf(TEXT("%llu"),HistoricalSnapshot.Start));Record->SetStringField(TEXT("source_end_us"),FString::Printf(TEXT("%llu"),HistoricalSnapshot.End));
        Record->SetNumberField(TEXT("scan_span_us"),HistoricalSnapshot.Duration);Record->SetNumberField(TEXT("stable_observation_us"),StaticWindow.StableMicros);
        TArray<TSharedPtr<FJsonValue>> Boots;for(const auto& Boot:HistoricalSnapshot.Boots)Boots.Add(MakeShared<FJsonValueString>(Boot));Record->SetArrayField(TEXT("source_boots"),Boots);
    }
    TArray<TSharedPtr<FJsonValue>> Angles;for (double V:Q) Angles.Add(MakeShared<FJsonValueNumber>(V));Record->SetArrayField(TEXT("q_radians"),Angles);
    Record->SetStringField(TEXT("type"),TEXT("posedoll.capture/1"));Record->SetStringField(TEXT("rig_runtime_sha256"),Adapter->Fingerprint);Record->SetStringField(TEXT("device"),LastIdentity.Device);Record->SetStringField(TEXT("session"),LastIdentity.Session);Record->SetStringField(TEXT("capability"),LastIdentity.Capability);Record->SetStringField(TEXT("sequence_number"),FString::Printf(TEXT("%llu"),LastSample.Sequence));
    TArray<TSharedPtr<FJsonValue>> Raw,Statuses;
    for(int32 I=0;I<LastSample.Raw.Num();++I) {Raw.Add(LastSample.Status[I]==TEXT("valid")?StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNumber>(LastSample.Raw[I])):StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()));Statuses.Add(MakeShared<FJsonValueString>(LastSample.Status[I]));}
    Record->SetArrayField(TEXT("raw_angles_rad"),Raw);Record->SetArrayField(TEXT("axis_status"),Statuses);
    Record->SetObjectField(TEXT("controls"),ControlData(CapturedPose.Controls));Record->SetObjectField(TEXT("raw_fk_controls"),ControlData(RawPose.Controls));
    auto Switches=MakeShared<FJsonObject>();for(const auto& P:CapturedPose.Switches) Switches->SetBoolField(P.Key.ToString(),P.Value);Record->SetObjectField(TEXT("switches"),Switches);
    Record->SetStringField(TEXT("constraint_solver"),TEXT("affected-chain IK to FK matching, selected joint-local rotation replacement"));Record->SetNumberField(TEXT("display_rate_numerator"),Movie->GetDisplayRate().Numerator);Record->SetNumberField(TEXT("display_rate_denominator"),Movie->GetDisplayRate().Denominator);Record->SetStringField(TEXT("interpolation"),Linear?TEXT("Linear"):TEXT("Constant"));
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("PoseDoll");IFileManager::Get().MakeDirectory(*Directory,true);
    if (!FFileHelper::SaveStringToFile(JsonString(Record),*(Directory/FString::Printf(TEXT("capture_%d_%s.json"),Frame,*FGuid::NewGuid().ToString(EGuidFormats::Digits))))) Error=TEXT("Keys saved, but capture provenance file could not be written");
    return true;
}
FString FSession::StatusJson() const
{
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("state"),State);O->SetStringField(TEXT("error"),Error);O->SetStringField(TEXT("editing_note"),EditingNote);O->SetBoolField(TEXT("valid"),bValid);O->SetStringField(TEXT("capture_mode"),TEXT("OneShot"));O->SetNumberField(TEXT("applied"),Applied);O->SetNumberField(TEXT("invalid"),Invalid);O->SetNumberField(TEXT("captures"),Captures);O->SetStringField(TEXT("mask"),Mask);
    O->SetStringField(TEXT("source_label"),SourceLabel());O->SetStringField(TEXT("user_status"),UserStatusLabel());
    TArray<TSharedPtr<FJsonValue>> Parts;for(const FString& P:CustomParts)Parts.Add(MakeShared<FJsonValueString>(P));O->SetArrayField(TEXT("custom_parts"),Parts);O->SetStringField(TEXT("sequence"),Sequence.IsValid()?Sequence->GetPathName():TEXT(""));O->SetStringField(TEXT("binding"),Binding.ToString());
    O->SetStringField(TEXT("snapshot_state"),StaticWindow.State);O->SetStringField(TEXT("capture_id"),StaticWindow.CaptureId);O->SetBoolField(TEXT("has_snapshot"),bHasSnapshot);O->SetBoolField(TEXT("snapshot_eligible"),bSnapshotEligible);O->SetStringField(TEXT("snapshot_captured_utc"),SnapshotCapturedUtc);O->SetNumberField(TEXT("snapshot_target_frame"),SnapshotFrame);O->SetNumberField(TEXT("snapshot_stable_us"),StaticWindow.StableMicros);O->SetNumberField(TEXT("snapshot_peak_deg"),StaticWindow.PeakDegrees);O->SetNumberField(TEXT("snapshot_drift_deg_s"),StaticWindow.DriftDegreesPerSecond);
    if(bHasSnapshot){O->SetNumberField(TEXT("snapshot_age_ms"),(FPlatformTime::Seconds()-HistoricalReceived)*1000);O->SetStringField(TEXT("snapshot_scan_id"),FString::Printf(TEXT("%llu"),HistoricalSnapshot.ScanId));}
    if (LastValidSeconds>0) O->SetNumberField(TEXT("sample_age_ms"),(FPlatformTime::Seconds()-LastValidSeconds)*1000);
    auto P95=[](TArray<double> Values){Values.Sort();return Values.Num()?Values[FMath::Min(Values.Num()-1,FMath::FloorToInt(Values.Num()*.95))]:0;};
    O->SetNumberField(TEXT("processing_p95_ms"),P95(ProcessingTimes));O->SetNumberField(TEXT("receive_to_apply_p95_ms"),P95(ReceiveLatency));
    O->SetNumberField(TEXT("preview_frames"),PreviewFrames);O->SetNumberField(TEXT("main_thread_with_preview_p95_ms"),P95(MainThreadTimes));O->SetNumberField(TEXT("receive_to_preview_p95_ms"),P95(PreviewLatency));
    if (Input) {const auto S=Input->Snapshot();O->SetNumberField(TEXT("received"),S.Received);O->SetNumberField(TEXT("rejected"),S.Rejected);O->SetNumberField(TEXT("retired_replies"),S.RetiredReplies);O->SetStringField(TEXT("session"),S.Identity.Session);O->SetBoolField(TEXT("static_source"),S.bStatic);}
    return JsonString(O);
}
void FSession::RecordPreview(double DurationMs)
{
    if (LastReceivedSeconds<=0) return;
    ++PreviewFrames;MainThreadTimes.Add(LastProcessingMs+DurationMs);PreviewLatency.Add((FPlatformTime::Seconds()-LastReceivedSeconds)*1000);
    if (MainThreadTimes.Num()>3600) {MainThreadTimes.RemoveAt(0,600,EAllowShrinking::No);PreviewLatency.RemoveAt(0,600,EAllowShrinking::No);}
}
FString FSession::DiagnosticText() const
{
    FString Text=TEXT("UE 独立解算 · raw / 校准 q（度）· 状态\n无效样本时 q 保留最后有效解；fixed 取能力配置，不当作传感器零读数。\n");
    for(int32 I=0;I<Profile.Axes.Num();++I)
    {
        const FString Status=DiagnosticSample.Status.IsValidIndex(I)?DiagnosticSample.Status[I]:TEXT("no sample");
        const FString Raw=Status==TEXT("valid") && DiagnosticSample.Raw.IsValidIndex(I)?FString::Printf(TEXT("%8.2f"),FMath::RadiansToDegrees(DiagnosticSample.Raw[I])):TEXT("       —");
        const FString Angle=Q.IsValidIndex(I)?FString::Printf(TEXT("%8.2f"),FMath::RadiansToDegrees(Q[I])):TEXT("       —");
        Text+=FString::Printf(TEXT("%-24s  %s  /  %s  %s\n"),*Profile.Axes[I].Id,*Raw,*Angle,*Status);
    }
    return Text;
}
FString FSession::PoseReport() const
{
    auto O=MakeShared<FJsonObject>();O->SetObjectField(TEXT("controls"),ControlData(Pose.Controls));
    O->SetObjectField(TEXT("bones"),ControlData(Pose.Bones));O->SetObjectField(TEXT("raw_fk_controls"),ControlData(RawPose.Controls));
    return JsonString(O);
}
FString FSession::KeyReport() const
{
    auto O=MakeShared<FJsonObject>();auto Controls=MakeShared<FJsonObject>();auto Rotations=MakeShared<FJsonObject>();auto KeyTimes=MakeShared<FJsonObject>();
    if (Track.IsValid()) for (auto* Base:Track->GetAllSections()) if (const auto* Section=Cast<UMovieSceneControlRigParameterSection>(Base))
    {
        for (const auto& E:Section->GetTransformParameterNamesAndCurves())
        {
            int32 Count=0;for (int32 I=0;I<3;++I) Count+=E.Translation[I].GetNumKeys()+E.Rotation[I].GetNumKeys()+E.Scale[I].GetNumKeys();Controls->SetNumberField(E.ParameterName.ToString(),Count);
            if (Count)
            {
                TArray<TSharedPtr<FJsonValue>> Axes,Times;
                for (const auto& C:E.Rotation) {TArray<TSharedPtr<FJsonValue>> Values;for(const auto& V:C.GetData().GetValues()) Values.Add(MakeShared<FJsonValueNumber>(V.Value));Axes.Add(MakeShared<FJsonValueArray>(Values));}
                for (const auto T:E.Rotation[0].GetData().GetTimes()) Times.Add(MakeShared<FJsonValueNumber>(T.Value));
                Rotations->SetArrayField(E.ParameterName.ToString(),Axes);KeyTimes->SetArrayField(E.ParameterName.ToString(),Times);
            }
        }
        for (const auto& E:Section->GetBoolParameterNamesAndCurves()) Controls->SetNumberField(E.ParameterName.ToString(),E.ParameterCurve.GetNumKeys());
    }
    O->SetObjectField(TEXT("control_keys"),Controls);O->SetObjectField(TEXT("rotation_keys_degrees"),Rotations);O->SetObjectField(TEXT("key_ticks"),KeyTimes);O->SetBoolField(TEXT("dirty"),Sequence.IsValid() && Sequence->GetPackage()->IsDirty());
    if (Sequence.IsValid()) {O->SetNumberField(TEXT("display_numerator"),Sequence->GetMovieScene()->GetDisplayRate().Numerator);O->SetNumberField(TEXT("display_denominator"),Sequence->GetMovieScene()->GetDisplayRate().Denominator);}
    return JsonString(O);
}
}

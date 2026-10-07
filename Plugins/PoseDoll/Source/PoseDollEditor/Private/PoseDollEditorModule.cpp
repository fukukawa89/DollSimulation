#include "Modules/ModuleManager.h"
#include "PoseDollSession.h"
#include "PoseDollEditorLibrary.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "PreviewScene.h"
#include "Components/PoseableMeshComponent.h"
#include "SceneManagement.h"
#include "Containers/Ticker.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/SkeletalMesh.h"

namespace
{
const FName PoseDollDebugTabName(TEXT("PoseDollDebug"));

TSharedRef<SWidget> SessionButton(const FString& Label,const FString& Action,const FString& Argument=TEXT(""))
{
    return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Action,Argument]
    {
        UPoseDollEditorLibrary::SessionCommand(Action,Argument);
        return FReply::Handled();
    });
}

class FPoseViewportClient : public FEditorViewportClient
{
public:
    FPoseViewportClient(FPreviewScene* Scene,const TSharedRef<SEditorViewport>& Widget):FEditorViewportClient(nullptr,Scene,Widget)
    {
        SetViewLocation(FVector(280,420,200));
        SetViewRotation((FVector(-50,0,95)-GetViewLocation()).Rotation());
        SetViewMode(VMI_Lit);
        SetRealtime(true);
        EngineShowFlags.SetSelectionOutline(false);
        // Keep the fixed preview background out of temporal auto-exposure feedback.
        // This is local to PoseDoll; scene and project exposure settings stay intact.
        EngineShowFlags.SetEyeAdaptation(false);
        ExposureSettings.bFixed=true;
        ExposureSettings.FixedEV100=0;
    }
    virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View,PDI);const auto& S=PoseDoll::FSession::Get();
        auto Point=[](const PoseDoll::FMatrix44& M){return FQuat(FVector::UpVector,UE_DOUBLE_PI/2).RotateVector(M.ToUnreal().GetLocation())*4+FVector(-150,0,0);};
        if (S.SourcePose.Num()==S.Profile.Nodes.Num()) for (int32 I=0;I<S.Profile.Nodes.Num();++I)
        {
            const int32 Parent=S.Profile.Nodes[I].Parent;if (Parent!=INDEX_NONE) PDI->DrawLine(Point(S.SourcePose[Parent]),Point(S.SourcePose[I]),FLinearColor(.1f,.8f,1.f),SDPG_World,3.f);
        }
    }
};
class SPoseViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SPoseViewport){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Scene=MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues());SEditorViewport::Construct(SEditorViewport::FArguments());
        auto& S=PoseDoll::FSession::Get();if (S.Initialize())
        {
            Mesh=NewObject<UPoseableMeshComponent>();Mesh->SetSkinnedAssetAndUpdate(S.Adapter->GetMesh());Scene->AddComponent(Mesh,FTransform::Identity);Mesh->SetCastShadow(true);
        }
    }
    virtual void Tick(const FGeometry& G,double Time,float Delta) override
    {
        SEditorViewport::Tick(G,Time,Delta);auto& S=PoseDoll::FSession::Get();
        if (Mesh && LastPreviewRevision!=S.PreviewRevision)
        {
            const double Begin=FPlatformTime::Seconds();const auto& Ref=Mesh->GetSkinnedAsset()->GetRefSkeleton();
            for (int32 I=0;I<Ref.GetNum();++I)
            {
                const FName Name=Ref.GetBoneName(I);const int32 Parent=Ref.GetParentIndex(I);
                if (const auto* Global=S.Pose.Bones.Find(Name))
                {
                    const auto* ParentGlobal=Parent!=INDEX_NONE?S.Pose.Bones.Find(Ref.GetBoneName(Parent)):nullptr;
                    Mesh->BoneSpaceTransforms[I]=ParentGlobal?Global->GetRelativeTransform(*ParentGlobal):*Global;
                }
            }
            Mesh->MarkRefreshTransformDirty();Mesh->RefreshBoneTransforms();LastPreviewRevision=S.PreviewRevision;S.RecordPreview((FPlatformTime::Seconds()-Begin)*1000);
        }
    }
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override {Client=MakeShared<FPoseViewportClient>(Scene.Get(),SharedThis(this));return Client.ToSharedRef();}
private:
    TUniquePtr<FPreviewScene> Scene;
    TSharedPtr<FPoseViewportClient> Client;
    UPoseableMeshComponent* Mesh=nullptr;
    uint64 LastPreviewRevision=MAX_uint64;
};
class SPoseDebugPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPoseDebugPanel){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        PoseDoll::FSession::Get().Initialize();
        RefreshDiagnostic();
        ChildSlot[SNew(SBox).MinDesiredWidth(560).MinDesiredHeight(420)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(10)[SNew(STextBlock).AutoWrapText(true)
                .Text(FText::FromString(TEXT("模拟器和离线测试会切换数据来源；采集仍在主面板中执行。关闭本面板会保留当前来源。")))]
            +SVerticalBox::Slot().AutoHeight().Padding(10,4)[SNew(STextBlock)
                .Text_Lambda([]{return FText::FromString(PoseDoll::FSession::Get().SourceLabel());})]
            +SVerticalBox::Slot().AutoHeight().Padding(10,6)[SNew(SWrapBox).UseAllottedSize(true)
                +SWrapBox::Slot().Padding(0,0,8,6)[SessionButton(TEXT("连接模拟器"),TEXT("connect"))]
                +SWrapBox::Slot().Padding(0,0,8,6)[SessionButton(TEXT("离线中立测试"),TEXT("fixture"),TEXT("neutral.sample.json"))]
                +SWrapBox::Slot().Padding(0,0,8,6)[SessionButton(TEXT("非对称全身测试"),TEXT("fixture"),TEXT("asymmetric_pose.sample.json"))]]
            +SVerticalBox::Slot().AutoHeight().Padding(10,4)[SNew(STextBlock).Text(FText::FromString(TEXT("接收、校准与会话诊断")))]
            +SVerticalBox::Slot().FillHeight(1).Padding(10)[SNew(SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true)
                .Text_Lambda([this]{return Diagnostic;})]]];
    }
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Time,Delta);
        if(Time-LastDiagnostic>.25){RefreshDiagnostic();LastDiagnostic=Time;}
    }
private:
    void RefreshDiagnostic()
    {
        const auto& S=PoseDoll::FSession::Get();
        Diagnostic=FText::FromString(TEXT("会话状态：")+S.State+TEXT("\n最近错误：")+S.Error+TEXT("\n")+
            S.SnapshotLabel()+TEXT("\n")+S.EditingNote+TEXT("\n\n")+S.DiagnosticText()+TEXT("\n")+S.StatusJson());
    }
    FText Diagnostic;
    double LastDiagnostic=0;
};

class SPosePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPosePanel){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        for (const TCHAR* M:{TEXT("FullBody"),TEXT("UpperBody"),TEXT("LowerBody"),TEXT("arm_l"),TEXT("arm_r"),TEXT("leg_l"),TEXT("leg_r"),TEXT("Custom")}) Masks.Add(MakeShared<FString>(M));
        Transitions.Add(MakeShared<FString>(TEXT("保持姿势")));
        Transitions.Add(MakeShared<FString>(TEXT("线性过渡")));
        TSharedRef<SWrapBox> PartPicker=SNew(SWrapBox).UseAllottedSize(true);
        for(const auto& Part:PoseDoll::FSession::PartOptions())
        {
            const FString Id=Part.Key,Label=Part.Value;
            PartPicker->AddSlot().Padding(5,3)[SNew(SBox).WidthOverride(115)
                [SNew(SCheckBox)
                    .IsChecked_Lambda([Id]
                    {
                        const auto& S=PoseDoll::FSession::Get();
                        if(S.Adapter)for(const auto& M:S.Adapter->GetMapping())if(M.Semantic==Id && S.IsMasked(M))return ECheckBoxState::Checked;
                        return ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([Id](ECheckBoxState State){PoseDoll::FSession::Get().TogglePart(Id,State==ECheckBoxState::Checked);})
                    [SNew(STextBlock).Text(FText::FromString(Label))]]];
        }
        ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("PoseDoll Lab")))]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("调试与测试")))
                .ToolTipText(FText::FromString(TEXT("打开模拟器、离线测试与详细诊断。")))
                .OnClicked_Lambda([]{FGlobalTabmanager::Get()->TryInvokeTab(PoseDollDebugTabName);return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("摆好人偶，在当前帧采集所选部位的姿势。采集后可继续在 Control Rig 中编辑。")))]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SessionButton(TEXT("连接静态人偶"),TEXT("connect"),TEXT("static"))]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SessionButton(TEXT("断开"),TEXT("disconnect"))]
            +SHorizontalBox::Slot().FillWidth(1).Padding(10,0).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true)
                .Text_Lambda([]{return FText::FromString(PoseDoll::FSession::Get().SourceLabel());})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([]{return FText::FromString(PoseDoll::FSession::Get().UserStatusLabel());})]
        +SVerticalBox::Slot().FillHeight(1).Padding(4)[SNew(SPoseViewport)]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SessionButton(TEXT("绑定所选角色 / 当前序列"),TEXT("bind_selection"))]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0,0,0).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("采集范围")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Masks)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> M){return SNew(STextBlock).Text(FText::FromString(PoseDoll::FSession::MaskLabel(*M)));})
                .OnSelectionChanged_Lambda([](TSharedPtr<FString> M,ESelectInfo::Type){if(M)UPoseDollEditorLibrary::SessionCommand(TEXT("mask"),*M);})
                [SNew(STextBlock).Text_Lambda([]{return FText::FromString(PoseDoll::FSession::MaskLabel(PoseDoll::FSession::Get().Mask));})]]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SWrapBox).UseAllottedSize(true)
            +SWrapBox::Slot().Padding(0,0,12,6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text_Lambda([]{return FText::FromString(TEXT("采集")+PoseDoll::FSession::Get().SelectionLabel());}).OnClicked_Lambda([this]{Capture(0);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).Text(FText::FromString(TEXT("采集并前进"))).OnClicked_Lambda([this]{Capture(Step);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(55)[SNew(SNumericEntryBox<int32>).MinValue(1).MaxValue(100).Value_Lambda([this]{return Step;}).OnValueChanged_Lambda([this](int32 V){Step=V;})]]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("帧")))]]
            +SWrapBox::Slot().Padding(0,0,0,6)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("关键帧过渡")))]
                +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Transitions).InitiallySelectedItem(Transitions[0])
                    .ToolTipText(FText::FromString(TEXT("用于本次采集写入的关键帧。保持姿势：到下一个关键帧时切换；线性过渡：在相邻关键帧之间插值。已有关键帧可在 Sequencer 中调整。")))
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Option){return SNew(STextBlock).Text(FText::FromString(*Option));})
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Option,ESelectInfo::Type){if(Option)Linear=Option==Transitions[1];})
                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(*Transitions[Linear?1:0]);})]]]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SExpandableArea).InitiallyCollapsed(true)
            .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("自定义采集部位（仅替换关节转角）")))]
            .BodyContent()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("清空选择"))).OnClicked_Lambda([]{PoseDoll::FSession::Get().SetCustomParts({});return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).Text(FText::FromString(TEXT("全选"))).OnClicked_Lambda([]{TSet<FString> Parts;for(const auto& P:PoseDoll::FSession::PartOptions())Parts.Add(P.Key);PoseDoll::FSession::Get().SetCustomParts(Parts);return FReply::Handled();})]]
                +SVerticalBox::Slot().AutoHeight()[PartPicker]]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([]{const auto& S=PoseDoll::FSession::Get();return FText::FromString((S.Sequence.IsValid()?TEXT("序列：")+S.Sequence->GetName():TEXT("尚未绑定序列"))+FString::Printf(TEXT("  · 已采集 %llu 次"),S.Captures));})]
        +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,6)[SNew(STextBlock).AutoWrapText(true)
            .Visibility_Lambda([]{return PoseDoll::FSession::Get().EditingNote.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
            .Text_Lambda([]{return FText::FromString(PoseDoll::FSession::Get().EditingNote);})]
        ];
    }
    ~SPosePanel() {PoseDoll::FSession::Get().Shutdown();}
private:
    void Capture(int32 Advance)
    {PoseDoll::FSession::Get().CaptureCurrent(Advance,Linear);}
    TArray<TSharedPtr<FString>> Masks,Transitions;int32 Step=4;bool Linear=false;
};
}
class FPoseDollEditorModule : public IModuleInterface
{
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle ReplacedHandle;
    FDelegateHandle ModifiedHandle;
    FDelegateHandle UndoHandle;
public:
    virtual void StartupModule() override
    {
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt){return PoseDoll::FSession::Get().Tick(Dt);}));
        ReplacedHandle=FCoreUObjectDelegates::OnObjectsReplaced.AddLambda([](const TMap<UObject*,UObject*>& Replaced){auto& S=PoseDoll::FSession::Get();if(S.Adapter && (Replaced.Contains(S.Adapter->GetRig()) || Replaced.Contains(S.Adapter->GetRig()->GetClass()) || Replaced.Contains(S.Component.Get()))){S.Shutdown();S.State=TEXT("Fault");S.Error=TEXT("Target/Rig was recompiled or replaced; rebind and validate before capturing");}});
        UndoHandle=FEditorDelegates::PostUndoRedo.AddLambda([]{PoseDoll::FSession::Get().AfterUndoRedo();});
        ModifiedHandle=FCoreUObjectDelegates::OnObjectModified.AddLambda([](UObject* Object){auto& S=PoseDoll::FSession::Get();S.ObserveObjectModified(Object);if(S.Adapter && Object==S.Adapter->GetRig()->GetClass()->ClassGeneratedBy){S.Shutdown();S.State=TEXT("Fault");S.Error=TEXT("Target Rig asset changed; rebind and validate before capturing");}});
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("PoseDollLab"),FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&){return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SPosePanel)];})).SetDisplayName(FText::FromString(TEXT("PoseDoll Lab")));
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PoseDollDebugTabName,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&){return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SPoseDebugPanel)];}))
            .SetDisplayName(FText::FromString(TEXT("调试与测试"))).SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FPoseDollEditorModule::RegisterMenus));
    }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("PoseDollLab"),FText::FromString(TEXT("PoseDoll Lab")),FText::FromString(TEXT("Open the pose capture workspace")),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([]{FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("PoseDollLab")));})));
    }
    virtual void ShutdownModule() override
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PoseDollDebugTabName);
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);FCoreUObjectDelegates::OnObjectsReplaced.Remove(ReplacedHandle);FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);FEditorDelegates::PostUndoRedo.Remove(UndoHandle);UToolMenus::UnRegisterStartupCallback(this);UToolMenus::UnregisterOwner(this);FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("PoseDollLab"));PoseDoll::FSession::Get().Shutdown();
    }
};
IMPLEMENT_MODULE(FPoseDollEditorModule,PoseDollEditor)

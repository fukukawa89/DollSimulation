#include "Modules/ModuleManager.h"
#include "PoseDollSession.h"
#include "PoseDollEditorLibrary.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
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
#include "Editor/EditorPerformanceSettings.h"

namespace
{
class FPoseViewportClient : public FEditorViewportClient
{
public:
    FPoseViewportClient(FPreviewScene* Scene,const TSharedRef<SEditorViewport>& Widget):FEditorViewportClient(nullptr,Scene,Widget)
    {SetViewLocation(FVector(280,420,200));SetViewRotation((FVector(-50,0,95)-GetViewLocation()).Rotation());SetViewMode(VMI_Lit);SetRealtime(true);EngineShowFlags.SetSelectionOutline(false);}
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
        if (Mesh && (LastApplied!=S.Applied || !Mesh->GetRelativeTransform().Equals(S.Placement)))
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
            Mesh->MarkRefreshTransformDirty();Mesh->RefreshBoneTransforms();Mesh->SetRelativeTransform(S.Placement);LastApplied=S.Applied;S.RecordPreview((FPlatformTime::Seconds()-Begin)*1000);
        }
    }
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override {Client=MakeShared<FPoseViewportClient>(Scene.Get(),SharedThis(this));return Client.ToSharedRef();}
private:
    TUniquePtr<FPreviewScene> Scene;
    TSharedPtr<FPoseViewportClient> Client;
    UPoseableMeshComponent* Mesh=nullptr;
    uint64 LastApplied=MAX_uint64;
};
class SPosePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPosePanel){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        // The companion simulator is normally foreground while the editor previews.
        // Keep the user's preference in memory and restore it when this panel closes.
        auto* Performance=GetMutableDefault<UEditorPerformanceSettings>();
        bPreviousBackgroundThrottle=Performance->bThrottleCPUWhenNotForeground;
        Performance->bThrottleCPUWhenNotForeground=false;
        for (const TCHAR* M:{TEXT("FullBody"),TEXT("UpperBody"),TEXT("arm_l"),TEXT("arm_r"),TEXT("leg_l"),TEXT("leg_r")}) Masks.Add(MakeShared<FString>(M));
        ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("PoseDoll Lab · 44 路机械姿势 → Manny Control Rig")))]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("连接模拟器"),TEXT("connect"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Live / 恢复"),TEXT("resume"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clutch 相对编辑"),TEXT("resume"),TEXT("clutch"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("冻结"),TEXT("freeze"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("断开"),TEXT("disconnect"))]]
        +SVerticalBox::Slot().FillHeight(1).Padding(4)[SNew(SPoseViewport)]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("绑定所选角色 / 当前序列"),TEXT("bind_selection"))]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Masks)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> M){return SNew(STextBlock).Text(FText::FromString(*M));})
                .OnSelectionChanged_Lambda([](TSharedPtr<FString> M,ESelectInfo::Type){if(M)UPoseDollEditorLibrary::SessionCommand(TEXT("mask"),*M);})
                [SNew(STextBlock).Text_Lambda([]{return FText::FromString(PoseDoll::FSession::Get().Mask);})]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Capture"))).OnClicked_Lambda([this]{Capture(0);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).Text(FText::FromString(TEXT("Capture + 前进"))).OnClicked_Lambda([this]{Capture(Step);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(55)[SNew(SNumericEntryBox<int32>).MinValue(1).MaxValue(100).Value_Lambda([this]{return Step;}).OnValueChanged_Lambda([this](int32 V){Step=V;})]]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("离线中立测试"),TEXT("fixture"),TEXT("neutral.sample.json"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("非对称全身测试"),TEXT("fixture"),TEXT("asymmetric_pose.sample.json"))]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text_Lambda([this]{return FText::FromString(Linear?TEXT("插值：Linear"):TEXT("插值：Constant"));}).OnClicked_Lambda([this]{Linear=!Linear;return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[ContactButton(TEXT("左脚"),TEXT("leg_l"))]
            +SHorizontalBox::Slot().AutoWidth()[ContactButton(TEXT("右脚"),TEXT("leg_r"))]
            +SHorizontalBox::Slot().AutoWidth()[ContactButton(TEXT("左手"),TEXT("arm_l"))]
            +SHorizontalBox::Slot().AutoWidth()[ContactButton(TEXT("右手"),TEXT("arm_r"))]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("场景放置  ")))]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("X cm"),0)]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("Y cm"),1)]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("Z cm"),2)]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("侧倾°"),3)]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("俯仰°"),4)]
            +SHorizontalBox::Slot().AutoWidth()[PlacementInput(TEXT("转向°"),5)]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([]{const auto& S=PoseDoll::FSession::Get();return FText::FromString(S.State+TEXT("  |  ")+S.Error+TEXT("\n")+(S.Sequence.IsValid()?S.Sequence->GetPathName():TEXT("尚未绑定序列"))+FString::Printf(TEXT("  · 应用 %llu 帧 · Capture %llu 次 · 接触残差 %.3f cm %s"),S.Applied,S.Captures,S.Pose.ContactPositionErrorCm,S.Pose.bContactsReachable?TEXT(""):TEXT("不可达")));})]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SExpandableArea).InitiallyCollapsed(true)
            .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("44 路接收与校准诊断")))]
            .BodyContent()[SNew(SBox).HeightOverride(230)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this]{return Diagnostic;})]]]]
        ];
    }
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Time,Delta);
        if(Time-LastDiagnostic>.25){LastDiagnostic=Time;Diagnostic=FText::FromString(PoseDoll::FSession::Get().DiagnosticText());}
    }
    ~SPosePanel() {GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground=bPreviousBackgroundThrottle;PoseDoll::FSession::Get().Shutdown();}
private:
    bool bPreviousBackgroundThrottle=true;
    FText Diagnostic;
    double LastDiagnostic=0;
    TSharedRef<SWidget> PlacementInput(const FString& Label,int32 Index)
    {
        return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Label))]
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(85)[SNew(SNumericEntryBox<double>).Value_Lambda([Index]{const auto& T=PoseDoll::FSession::Get().Placement;return Index<3?T.GetLocation()[Index]:T.Rotator().Euler()[Index-3];})
            .OnValueChanged_Lambda([Index](double V){auto& S=PoseDoll::FSession::Get();if(!S.Contacts.IsEmpty()){S.Error=TEXT("先解除接触锁定，再调整场景放置");return;}if(Index<3){auto P=S.Placement.GetLocation();P[Index]=V;S.Placement.SetLocation(P);}else{auto R=S.Placement.Rotator().Euler();R[Index-3]=V;S.Placement.SetRotation(FQuat::MakeFromEuler(R));}})]];
    }
    TSharedRef<SWidget> ContactButton(const FString& Label,const FString& Chain)
    {
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text_Lambda([Label,Chain]{return FText::FromString(Label+(PoseDoll::FSession::Get().Contacts.Contains(Chain)?TEXT("：已锁定"):TEXT("：自由")));}).OnClicked_Lambda([Chain]{auto& S=PoseDoll::FSession::Get();S.Contact(Chain,!S.Contacts.Contains(Chain));return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight()[SNew(SButton)
                .IsEnabled_Lambda([Chain]{return PoseDoll::FSession::Get().Contacts.Contains(Chain);})
                .Text_Lambda([Chain]{const auto* Goal=PoseDoll::FSession::Get().Contacts.Find(Chain);return FText::FromString(Goal && !Goal->bLockRotation?TEXT("方向：自由"):TEXT("方向：锁定"));})
                .ToolTipText(FText::FromString(TEXT("保留接触位置，切换是否同时锁定手掌或脚掌方向")))
                .OnClicked_Lambda([Chain]{if(auto* Goal=PoseDoll::FSession::Get().Contacts.Find(Chain)) Goal->bLockRotation=!Goal->bLockRotation;return FReply::Handled();})];
    }
    TSharedRef<SWidget> Button(const FString& Label,const FString& Action,const FString& Argument=TEXT(""))
    {return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Action,Argument]{UPoseDollEditorLibrary::SessionCommand(Action,Argument);return FReply::Handled();});}
    void Capture(int32 Advance)
    {auto& S=PoseDoll::FSession::Get();const int32 Frame=ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame.FrameNumber.Value;S.Capture(Frame,Advance,Linear);}
    TArray<TSharedPtr<FString>> Masks;int32 Step=4;bool Linear=false;
};
}
class FPoseDollEditorModule : public IModuleInterface
{
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle ReplacedHandle;
    FDelegateHandle ModifiedHandle;
public:
    virtual void StartupModule() override
    {
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt){return PoseDoll::FSession::Get().Tick(Dt);}));
        ReplacedHandle=FCoreUObjectDelegates::OnObjectsReplaced.AddLambda([](const TMap<UObject*,UObject*>& Replaced){auto& S=PoseDoll::FSession::Get();if(S.Adapter && (Replaced.Contains(S.Adapter->GetRig()) || Replaced.Contains(S.Adapter->GetRig()->GetClass()) || Replaced.Contains(S.Component.Get()))){S.Shutdown();S.State=TEXT("Fault");S.Error=TEXT("Target/Rig was recompiled or replaced; rebind and validate before resuming");}});
        ModifiedHandle=FCoreUObjectDelegates::OnObjectModified.AddLambda([](UObject* Object){auto& S=PoseDoll::FSession::Get();if(S.Adapter && Object==S.Adapter->GetRig()->GetClass()->ClassGeneratedBy){S.Shutdown();S.State=TEXT("Fault");S.Error=TEXT("Target Rig asset changed; rebind and validate before resuming");}});
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("PoseDollLab"),FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&){return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SPosePanel)];})).SetDisplayName(FText::FromString(TEXT("PoseDoll Lab")));
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FPoseDollEditorModule::RegisterMenus));
    }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("PoseDollLab"),FText::FromString(TEXT("PoseDoll Lab")),FText::FromString(TEXT("Open the pose capture workspace")),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([]{FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("PoseDollLab")));})));
    }
    virtual void ShutdownModule() override
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);FCoreUObjectDelegates::OnObjectsReplaced.Remove(ReplacedHandle);FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);UToolMenus::UnRegisterStartupCallback(this);UToolMenus::UnregisterOwner(this);FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("PoseDollLab"));PoseDoll::FSession::Get().Shutdown();
    }
};
IMPLEMENT_MODULE(FPoseDollEditorModule,PoseDollEditor)

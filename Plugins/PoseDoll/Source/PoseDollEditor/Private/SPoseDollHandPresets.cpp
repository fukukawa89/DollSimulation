#include "SPoseDollHandPresets.h"
#include "PoseDollSession.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/FileManager.h"
namespace PoseDoll
{
TSharedRef<SWidget> SPoseDollHandPresets::FilterButton(const FString& Label,TFunction<bool()> Active,TFunction<void()> Pick)
{
    return SNew(SButton).ContentPadding(FMargin(9,5))
        .ButtonColorAndOpacity_Lambda([Active]{return Active()?FLinearColor(.08f,.42f,.62f):FLinearColor(.12f,.13f,.15f);})
        .OnClicked_Lambda([Pick]{Pick();return FReply::Handled();})
        [SNew(STextBlock).Text(FText::FromString(Label))];
}
TSharedRef<SWidget> SPoseDollHandPresets::CountFilter(int32 Index,const FString& Label)
{
    auto Row=SNew(SHorizontalBox);
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(55)[SNew(STextBlock).Text(FText::FromString(Label))]];
    for(int32 N=-1;N<=5;++N)Row->AddSlot().AutoWidth().Padding(0,0,4,0)
        [FilterButton(N<0?TEXT("不限"):FString::FromInt(N),[this,Index,N]{return Counts[Index]==N;},[this,Index,N]{Counts[Index]=N;Rebuild();})];
    return Row;
}
TSharedRef<SWidget> SPoseDollHandPresets::FingerFilter(int32 Index,const FString& Label)
{
    auto Row=SNew(SHorizontalBox);
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(55)[SNew(STextBlock).Text(FText::FromString(Label))]];
    const TCHAR States[]={0,'E','B','C'};const TCHAR* Labels[]={TEXT("不限"),TEXT("伸出"),TEXT("半弯"),TEXT("蜷曲")};
    for(int32 I=0;I<4;++I){const TCHAR State=States[I];Row->AddSlot().AutoWidth().Padding(0,0,4,0)
        [FilterButton(Labels[I],[this,Index,State]{return Fingers[Index]==State;},[this,Index,State]{Fingers[Index]=State;Rebuild();})];}
    return Row;
}
const FSlateBrush* SPoseDollHandPresets::Brush(const FHandPreset& Preset,EHandSide ForSide)
{
    const FString Path=Preset.ImagePath(ForSide);
    if(!Images.Contains(Path)&&IFileManager::Get().FileExists(*Path))Images.Add(Path,MakeShared<FSlateDynamicImageBrush>(FName(*Path),FVector2D(384,384)));
    const auto* Found=Images.Find(Path);return Found?Found->Get():FAppStyle::GetBrush("WhiteBrush");
}
void SPoseDollHandPresets::Construct(const FArguments& Args)
{
    Linear=Args._Linear;FHandPresetLibrary::Get().Load(Error);
    auto Sides=SNew(SHorizontalBox);
    for(EHandSide Value:{EHandSide::Left,EHandSide::Right,EHandSide::Both})Sides->AddSlot().AutoWidth().Padding(0,0,6,0)
        [FilterButton(Value==EHandSide::Left?TEXT("左手"):Value==EHandSide::Right?TEXT("右手"):TEXT("双手"),[this,Value]{return Side==Value;},[this,Value]{Side=Value;Rebuild();})];
    auto Filters=SNew(SVerticalBox);
    Filters->AddSlot().AutoHeight().Padding(0,0,0,10)[SNew(STextBlock).Text(FText::FromString(TEXT("1  手指状态数量")))];
    const FString CountLabels[]={TEXT("伸出"),TEXT("半弯"),TEXT("蜷曲")};
    for(int32 I=0;I<3;++I)Filters->AddSlot().AutoHeight().Padding(0,0,0,6)[CountFilter(I,CountLabels[I])];
    Filters->AddSlot().AutoHeight().Padding(0,16,0,10)[SNew(STextBlock).Text(FText::FromString(TEXT("2  对应哪些手指")))];
    const FString FingerLabels[]={TEXT("拇指"),TEXT("食指"),TEXT("中指"),TEXT("无名指"),TEXT("小指")};
    for(int32 I=0;I<5;++I)Filters->AddSlot().AutoHeight().Padding(0,0,0,6)[FingerFilter(I,FingerLabels[I])];
    Filters->AddSlot().AutoHeight().Padding(0,12,0,0)[SNew(SButton).Text(FText::FromString(TEXT("重置筛选"))).OnClicked_Lambda([this]{for(int32& N:Counts)N=-1;for(TCHAR& S:Fingers)S=0;Rebuild();return FReply::Handled();})];
    ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(16,12)[Sides]
        +SVerticalBox::Slot().AutoHeight().Padding(16,0,16,12)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("按手指形状筛选，点击图片预览。应用后覆盖当前帧所选手的手指姿势。")))]
        +SVerticalBox::Slot().FillHeight(1).Padding(16,0)
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)[SNew(SScrollBox)+SScrollBox::Slot()[Filters]]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%d 个姿势"),Visible));})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).AutoWrapText(true)
                    .Visibility_Lambda([this]{return Visible==0?EVisibility::Visible:EVisibility::Collapsed;})
                    .Text(FText::FromString(TEXT("没有符合条件的姿势，请放宽筛选条件。")))]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Grid,SWrapBox).UseAllottedSize(true)]]]
            +SHorizontalBox::Slot().AutoWidth().Padding(16,0,0,0)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(250).HeightOverride(250)
                    [SNew(SImage).Image_Lambda([this]{return Selected?Brush(*Selected,Side==EHandSide::Right?EHandSide::Right:EHandSide::Left):FAppStyle::GetBrush("NoBrush");})]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Side==EHandSide::Both?TEXT("双手分别使用对应的镜像姿势"):TEXT(""));})]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(250).HeightOverride(250)
                    .Visibility_Lambda([this]{return Side==EHandSide::Both&&Selected?EVisibility::Visible:EVisibility::Collapsed;})
                    [SNew(SImage).Image_Lambda([this]{return Selected?Brush(*Selected,EHandSide::Right):FAppStyle::GetBrush("NoBrush");})]]]]
        +SVerticalBox::Slot().AutoHeight().Padding(16,12,16,8)[SNew(STextBlock).AutoWrapText(true).Text(this,&SPoseDollHandPresets::StatusText)]
        +SVerticalBox::Slot().AutoHeight().Padding(16,0,16,16)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SCheckBox)
                .IsChecked_Lambda([this]{return Linear?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){Linear=State==ECheckBoxState::Checked;})
                [SNew(STextBlock).Text(FText::FromString(TEXT("线性过渡（未勾选时保持姿势）")))]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ContentPadding(FMargin(20,9))
                .IsEnabled_Lambda([this]{return Selected.IsValid()&&FSession::Get().Sequence.IsValid()&&FSession::Get().Component.IsValid();})
                .Text_Lambda([this]{return FText::FromString(Side==EHandSide::Left?TEXT("应用到左手"):Side==EHandSide::Right?TEXT("应用到右手"):TEXT("应用到双手"));})
                .OnClicked_Lambda([this]{if(Selected){FSession::Get().ApplyHandPreset(Selected->Id,Side,Linear);Error=FSession::Get().Error;}return FReply::Handled();})]]];
    Rebuild();
}
void SPoseDollHandPresets::Rebuild()
{
    if(!Grid)return;Grid->ClearChildren();Visible=0;bool Found=false;
    for(const auto& P:FHandPresetLibrary::Get().All())
    {
        bool Match=true;const TCHAR States[]={'E','B','C'};
        for(int32 I=0;I<3;++I)if(Counts[I]>=0&&P->Count(States[I])!=Counts[I])Match=false;
        for(int32 I=0;I<5;++I)if(Fingers[I]&&P->States[I]!=Fingers[I])Match=false;
        if(!Match)continue;++Visible;Found|=Selected==P;
        const EHandSide ImageSide=Side==EHandSide::Right?EHandSide::Right:EHandSide::Left;
        Grid->AddSlot().Padding(0,0,8,8)[SNew(SBorder).Padding(3)
            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this,P]{return Selected==P?FLinearColor(.08f,.6f,.9f):FLinearColor(.10f,.11f,.13f);})
            [SNew(SButton).ContentPadding(0).OnClicked_Lambda([this,P]{Selected=P;Error.Empty();return FReply::Handled();})
                .ToolTipText(FText::FromString(FString::Printf(TEXT("伸出 %d · 半弯 %d · 蜷曲 %d"),P->Count('E'),P->Count('B'),P->Count('C'))))
                [SNew(SBox).WidthOverride(116).HeightOverride(116)[SNew(SImage).Image(Brush(*P,ImageSide))]]]];
    }
    if(!Found)Selected.Reset();
}
FText SPoseDollHandPresets::StatusText() const
{
    if(!Error.IsEmpty())return FText::FromString(Error);
    const auto& Session=FSession::Get();
    if(!Session.Sequence.IsValid()||!Session.Component.IsValid())return FText::FromString(TEXT("先在主面板绑定所选角色与当前序列，再应用预设。"));
    if(!Selected)return FText::FromString(TEXT("选择一张图片后应用；可用编辑器的撤销恢复。"));
    return FText::FromString(Session.EditingNote.IsEmpty()?TEXT("仅替换手指，保留手腕和身体姿势。"):Session.EditingNote);
}
}

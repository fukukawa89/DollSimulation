#include "SPoseDollBodyPicker.h"
#include "PoseDollSession.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace PoseDoll
{
namespace CaptureBodyMap
{
const TArray<FCaptureBodyRegion>& Regions()
{
    // Front view. Left/right always refer to the character, not the observer.
    static const TArray<FCaptureBodyRegion> Parts = {
        {TEXT("pelvis"), TEXT("躯干与双腿连接处"), {{153,257},{180,241},{207,257}}},
        {TEXT("waist"), TEXT("胸部下方至骨盆上方"), {{180,199},{180,227}}},
        {TEXT("chest"), TEXT("躯干中上段"), {{180,133},{180,183}}},
        {TEXT("head"), TEXT("颈部与头部"), {{180,51},{180,94}}},
        {TEXT("clavicle_l"), TEXT("胸口到左肩端的锁骨段"), {{191,110},{226,117}}},
        {TEXT("upperarm_l"), TEXT("左肩至左肘"), {{234,131},{258,181}}},
        {TEXT("lowerarm_l"), TEXT("左肘至左腕"), {{264,196},{287,248}}},
        {TEXT("hand_l"), TEXT("左前臂末端与手部连接处"), {{292,265},{300,286}}},
        {TEXT("clavicle_r"), TEXT("胸口到右肩端的锁骨段"), {{169,110},{134,117}}},
        {TEXT("upperarm_r"), TEXT("右肩至右肘"), {{126,131},{102,181}}},
        {TEXT("lowerarm_r"), TEXT("右肘至右腕"), {{96,196},{73,248}}},
        {TEXT("hand_r"), TEXT("右前臂末端与手部连接处"), {{68,265},{60,286}}},
        {TEXT("thigh_l"), TEXT("左髋至左膝"), {{207,273},{214,343}}},
        {TEXT("calf_l"), TEXT("左膝至左踝"), {{216,360},{218,413}}},
        {TEXT("foot_l"), TEXT("左小腿末端与足部连接处"), {{219,432},{227,447}}},
        {TEXT("ball_l"), TEXT("左脚前端"), {{242,459},{273,459}}},
        {TEXT("thigh_r"), TEXT("右髋至右膝"), {{153,273},{146,343}}},
        {TEXT("calf_r"), TEXT("右膝至右踝"), {{144,360},{142,413}}},
        {TEXT("foot_r"), TEXT("右小腿末端与足部连接处"), {{141,432},{133,447}}},
        {TEXT("ball_r"), TEXT("右脚前端"), {{118,459},{87,459}}}
    };
    return Parts;
}

static double Scale(const FVector2D& Size)
{
    return FMath::Max(0.0, FMath::Min(Size.X / 360.0, Size.Y / 480.0));
}

FVector2D Project(const FVector2D& Point, const FVector2D& Size)
{
    const double S = Scale(Size);
    return (Size - FVector2D(360,480) * S) * .5 + Point * S;
}

int32 HitTest(const FVector2D& LocalPosition, const FVector2D& Size)
{
    if (Size.X <= 0 || Size.Y <= 0 || LocalPosition.X < 0 || LocalPosition.Y < 0 ||
        LocalPosition.X > Size.X || LocalPosition.Y > Size.Y) return INDEX_NONE;

    int32 Best = INDEX_NONE;
    double BestDistance = TNumericLimits<double>::Max();
    const auto& Parts = Regions();
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        const auto& Part = Parts[Index];
        const double Radius = Part.Part == TEXT("head") ? 23.0 : 15.0;
        for (int32 P = 1; P < Part.Points.Num(); ++P)
        {
            const FVector2D A = Project(Part.Points[P-1], Size);
            const FVector2D B = Project(Part.Points[P], Size);
            const FVector2D AB = B - A;
            const double T = FMath::Clamp(FVector2D::DotProduct(LocalPosition - A, AB) /
                FMath::Max(AB.SizeSquared(), UE_SMALL_NUMBER), 0.0, 1.0);
            const double Distance = (LocalPosition - (A + AB * T)).SizeSquared();
            if (Distance <= Radius * Radius && Distance < BestDistance)
            {
                BestDistance = Distance;
                Best = Index;
            }
        }
    }
    return Best;
}
}

static FString CapturePartLabel(const FString& Id)
{
    for (const auto& Part : FSession::PartOptions()) if (Part.Key == Id) return Part.Value;
    return Id;
}

DECLARE_DELEGATE_OneParam(FOnCapturePartHovered, int32);

class SPoseDollBodyDiagram : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPoseDollBodyDiagram) {}
        SLATE_EVENT(FOnCapturePartHovered, OnPartHovered)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OnPartHovered = Args._OnPartHovered;
        SetVisibility(EVisibility::Visible);
        SetCursor(EMouseCursor::Hand);
        TSharedRef<SCanvas> Canvas = SNew(SCanvas);
        const auto& Parts = CaptureBodyMap::Regions();
        for (int32 Index = 0; Index < Parts.Num(); ++Index)
        {
            TSharedPtr<SButton> Button;
            Canvas->AddSlot()
                .Position_Lambda([this,Index] { return ButtonBounds(Index).Min; })
                .Size_Lambda([this,Index] { return ButtonBounds(Index).GetSize(); })
                [SAssignNew(Button, SButton)
                    .ButtonStyle(FAppStyle::Get(), "NoBorder")
                    .ContentPadding(0)
                    .ToolTipText_Lambda([this,Index]
                    {
                        const auto& Region = CaptureBodyMap::Regions()[Hovered == INDEX_NONE ? Index : Hovered];
                        return FText::FromString(CapturePartLabel(Region.Part) + TEXT("：") + Region.Description);
                    })
                    .OnClicked_Lambda([this,Index] { Toggle(Index); return FReply::Handled(); })
                    [SNew(SSpacer)]];
            Button->SetTag(FName(*(TEXT("PoseDoll.Part.") + Parts[Index].Part)));
#if WITH_ACCESSIBILITY
            Button->SetAccessibleBehavior(EAccessibleBehavior::Custom,
                TAttribute<FText>::CreateLambda([this,Index]
                {
                    const FString& Part = CaptureBodyMap::Regions()[Index].Part;
                    return FText::FromString(CapturePartLabel(Part) + (Selected.Contains(Part) ? TEXT("，已选") : TEXT("，未选")));
                }));
#endif
            Buttons.Add(Button);
        }
        ChildSlot[Canvas];
    }

    void SetSelection(const TSet<FString>& Parts)
    {
        Selected = Parts;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override
    {
        SCompoundWidget::Tick(Geometry, Time, DeltaTime);
        if (!MapSize.Equals(Geometry.GetLocalSize(), .1))
        {
            MapSize = Geometry.GetLocalSize();
            Invalidate(EInvalidateWidgetReason::Layout);
        }
        int32 Focused = INDEX_NONE;
        for (int32 Index = 0; Index < Buttons.Num(); ++Index)
        {
            if (Buttons[Index]->HasKeyboardFocus()) { Focused = Index; break; }
        }
        if (Focused != LastFocused)
        {
            LastFocused = Focused;
            if (Focused != INDEX_NONE) OnPartHovered.ExecuteIfBound(Focused);
            Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(315,420); }

    virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        const int32 Hit = CaptureBodyMap::HitTest(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), Geometry.GetLocalSize());
        // Consume blank space too: rectangular keyboard targets must not override geometric picking.
        if (Hit == INDEX_NONE) return FReply::Handled();
        // Like viewport selection, pick immediately on press. Native buttons remain keyboard targets.
        Toggle(Hit);
        return FReply::Handled().SetUserFocus(Buttons[Hit].ToSharedRef(), EFocusCause::Mouse);
    }

    virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        return OnPreviewMouseButtonDown(Geometry, Event);
    }

    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        const int32 Next = CaptureBodyMap::HitTest(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), Geometry.GetLocalSize());
        if (Next != Hovered)
        {
            Hovered = Next;
            if (Hovered != INDEX_NONE) OnPartHovered.ExecuteIfBound(Hovered);
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        return FReply::Unhandled();
    }

    virtual void OnMouseLeave(const FPointerEvent& Event) override
    {
        Hovered = INDEX_NONE;
        Invalidate(EInvalidateWidgetReason::Paint);
        SCompoundWidget::OnMouseLeave(Event);
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const override
    {
        const FVector2D Size = Geometry.GetLocalSize();
        const float Scale = static_cast<float>(CaptureBodyMap::Scale(Size));
        // MakeLines takes screen-pixel thickness; box geometry is already scaled by Slate.
        const float PixelScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        const FLinearColor Background = FStyleColors::Panel.GetSpecifiedColor();
        const FLinearColor Body = FMath::Lerp(Background, FStyleColors::Foreground.GetSpecifiedColor(), .10f);
        const FLinearColor Bone = FStyleColors::Foreground.GetSpecifiedColor() * FLinearColor(1,1,1,.65f);
        const FLinearColor Blue = FStyleColors::AccentBlue.GetSpecifiedColor();
        const FLinearColor Hover = FStyleColors::AccentOrange.GetSpecifiedColor();
        static const FSlateRoundedBoxBrush RoundBrush(FLinearColor::White);
        auto Disc = [&](FVector2D Center, FVector2D Extent, FLinearColor Color, int32 AtLayer)
        {
            FSlateDrawElement::MakeBox(Elements, AtLayer,
                Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Center - Extent * .5)), &RoundBrush,
                ESlateDrawEffect::None, Color * Tint);
        };
        auto Line = [&](const TArray<FVector2D>& Points, float Thickness, FLinearColor Color, int32 AtLayer, bool Rounded)
        {
            TArray<FVector2D> Projected;
            for (const auto& Point : Points) Projected.Add(CaptureBodyMap::Project(Point, Size));
            FSlateDrawElement::MakeLines(Elements, AtLayer, Geometry.ToPaintGeometry(), Projected,
                ESlateDrawEffect::None, Color * Tint, true, Thickness * Scale * PixelScale);
            if (Rounded) for (const auto& Point : Projected) Disc(Point, FVector2D(Thickness,Thickness)*Scale, Color, AtLayer);
        };
        Disc(CaptureBodyMap::Project({180,51},Size), FVector2D(46,58)*Scale, Body, Layer);
        Line({{180,86},{180,119},{180,213},{180,247}},64,Body,Layer,true);
        Line({{136,120},{180,127},{224,120}},43,Body,Layer,true);
        for (const double Side : {-1.0,1.0})
        {
            auto Mirror = [Side](TArray<FVector2D> Points)
            {
                for (auto& Point : Points) Point.X = 180 + Side * Point.X;
                return Points;
            };
            Line(Mirror({{47,123},{79,188},{110,255},{120,281}}),23,Body,Layer,true);
            Line(Mirror({{23,253},{33,348},{38,432}}),32,Body,Layer,true);
            Line(Mirror({{38,439},{53,459},{93,459}}),20,Body,Layer,true);
            for (double Y : {143.0,159.0,175.0})
                Line(Mirror({{7,Y-3},{28,Y},{32,Y-12}}),1.3f,Bone*.6f,Layer+1,false);
            Line(Mirror({{0,101},{47,123},{80,188},{110,255},{120,286}}),1.3f,Bone*.5f,Layer+1,false);
            Line(Mirror({{0,244},{23,259},{34,351},{39,428},{52,451},{93,459}}),1.3f,Bone*.5f,Layer+1,false);
        }
        const auto& Regions = CaptureBodyMap::Regions();
        for (int32 Index = 0; Index < Regions.Num(); ++Index)
        {
            const auto& Region = Regions[Index];
            const bool IsSelected = Selected.Contains(Region.Part);
            const bool Focused = Buttons[Index]->HasKeyboardFocus();
            if (Hovered == Index || Focused)
                Line(Region.Points, 13, Hover.CopyWithNewOpacity(.4f), Layer+2, true);
            const FLinearColor Color = IsSelected ? Blue : Bone;
            Line(Region.Points, IsSelected ? 8.f : 5.f, Color, Layer+3, true);
            for (const auto& Point : {Region.Points[0],Region.Points.Last()})
            {
                const FVector2D Center = CaptureBodyMap::Project(Point, Size);
                Disc(Center,FVector2D(10,10)*Scale,Color,Layer+3);
                if (!IsSelected) Disc(Center,FVector2D(6,6)*Scale,Background,Layer+4);
            }
        }
        return SCompoundWidget::OnPaint(Args,Geometry,CullingRect,Elements,Layer+5,Style,ParentEnabled);
    }

private:
    FBox2D ButtonBounds(int32 Index) const
    {
        FBox2D Bounds(EForceInit::ForceInit);
        for (const auto& Point : CaptureBodyMap::Regions()[Index].Points) Bounds += CaptureBodyMap::Project(Point,MapSize);
        return Bounds.ExpandBy(12.0);
    }
    void Toggle(int32 Index)
    {
        const FString& Part = CaptureBodyMap::Regions()[Index].Part;
        auto& Session = FSession::Get();
        Session.TogglePart(Part,!Session.GetSelectedParts().Contains(Part));
        SetSelection(Session.GetSelectedParts());
        OnPartHovered.ExecuteIfBound(Index);
    }
    FVector2D MapSize = FVector2D(315,420);
    TArray<TSharedPtr<SButton>> Buttons;
    TSet<FString> Selected;
    FOnCapturePartHovered OnPartHovered;
    int32 Hovered = INDEX_NONE, LastFocused = INDEX_NONE;
};

void SPoseDollBodyPicker::Construct(const FArguments& Args)
{
    DiagramHeight = Args._DiagramHeight;
    PartName = FText::FromString(TEXT("指向或选择一个部位"));
    PartDescription = FText::FromString(TEXT("左右按角色自身区分"));
    ChildSlot
    [SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FStyleColors::Panel).Padding(12)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()
            [SNew(SWrapBox).UseAllottedSize(true)
                +SWrapBox::Slot().Padding(0,0,8,8)
                [SNew(SButton).Text(FText::FromString(TEXT("全选"))).OnClicked_Lambda([this]
                    {TSet<FString> Parts;for(const auto& P:FSession::PartOptions())Parts.Add(P.Key);FSession::Get().SetCustomParts(Parts);RefreshSelection();return FReply::Handled();})]
                +SWrapBox::Slot().Padding(0,0,16,8)
                [SNew(SButton).Text(FText::FromString(TEXT("清空选择"))).OnClicked_Lambda([this]
                    {FSession::Get().SetCustomParts({});RefreshSelection();return FReply::Handled();})]
                +SWrapBox::Slot().Padding(0,3,0,8)
                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("已选 %d / %d 个部位"),SelectedCount,FSession::PartOptions().Num()));})]]
            +SVerticalBox::Slot().AutoHeight()
            [SNew(SWrapBox).UseAllottedSize(true)
                +SWrapBox::Slot().Padding(0,0,12,8)
                [SNew(SBox).WidthOverride_Lambda([this]{return FMath::Clamp(AvailableWidth-36.f,200.f,340.f);})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()
                        [SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(TEXT("角色右侧 R")))]
                            +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("正面")))]
                            +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)[SNew(STextBlock).Text(FText::FromString(TEXT("角色左侧 L")))]]
                        +SVerticalBox::Slot().AutoHeight()
                        [SNew(SBox).HeightOverride_Lambda([this]{return DiagramHeight.Get();})[SAssignNew(Diagram,SPoseDollBodyDiagram)
                            .OnPartHovered_Lambda([this](int32 Index){ShowPart(Index);})]]
                        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("蓝色为已选 · 再次点击取消")))]]]
                +SWrapBox::Slot().Padding(0,4,0,8)
                [SNew(SBox).WidthOverride_Lambda([this]{return AvailableWidth<560.f?FMath::Max(200.f,AvailableWidth-24.f):180.f;})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text(FText::FromString(TEXT("已选部位")))]
                        +SVerticalBox::Slot().AutoHeight()[SAssignNew(SelectedLabels,SWrapBox).UseAllottedSize(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,20,0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("当前部位")))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return PartName;})]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return PartDescription;})]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,20,0,4)[SNew(STextBlock).Text(FText::FromString(TEXT("采集内容")))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("所选部位的局部转角")))]]]]]];
    SelectionKey = TEXT("<initial>");
    RefreshSelection();
}

void SPoseDollBodyPicker::Tick(const FGeometry& Geometry, double Time, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,Time,DeltaTime);
    if (!FMath::IsNearlyEqual(AvailableWidth,static_cast<float>(Geometry.GetLocalSize().X),1.f))
    {
        AvailableWidth = static_cast<float>(Geometry.GetLocalSize().X);
        Invalidate(EInvalidateWidgetReason::Layout);
    }
    RefreshSelection();
}

void SPoseDollBodyPicker::RefreshSelection()
{
    const TSet<FString> Selected = FSession::Get().GetSelectedParts();
    FString Key;
    for (const auto& Part : FSession::PartOptions()) if (Selected.Contains(Part.Key)) Key += Part.Key + TEXT(";");
    if (Key == SelectionKey) return;
    SelectionKey = Key;
    SelectedCount = Selected.Num();
    Diagram->SetSelection(Selected);
    SelectedLabels->ClearChildren();
    for (const auto& Part : FSession::PartOptions())
    {
        if (!Selected.Contains(Part.Key)) continue;
        SelectedLabels->AddSlot().Padding(0,0,5,5)
            [SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FStyleColors::AccentBlue.GetSpecifiedColor().CopyWithNewOpacity(.18f))
                .Padding(FMargin(6,3))
                [SNew(STextBlock).Text(FText::FromString(TEXT("✓ ") + Part.Value))]];
    }
    if (Selected.IsEmpty())
        SelectedLabels->AddSlot()[SNew(STextBlock).Text(FText::FromString(TEXT("尚未选择部位")))];
}

void SPoseDollBodyPicker::ShowPart(int32 Index)
{
    if (!CaptureBodyMap::Regions().IsValidIndex(Index)) return;
    const auto& Region = CaptureBodyMap::Regions()[Index];
    PartName = FText::FromString(CapturePartLabel(Region.Part));
    PartDescription = FText::FromString(Region.Description);
}
}

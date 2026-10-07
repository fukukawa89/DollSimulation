#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWrapBox;
namespace PoseDoll
{
struct FCaptureBodyRegion
{
    FString Part;
    FString Description;
    TArray<FVector2D> Points;
};

namespace CaptureBodyMap
{
    const TArray<FCaptureBodyRegion>& Regions();
    FVector2D Project(const FVector2D& Point, const FVector2D& Size);
    int32 HitTest(const FVector2D& LocalPosition, const FVector2D& Size);
}

class SPoseDollBodyDiagram;
class SPoseDollBodyPicker : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPoseDollBodyPicker) : _DiagramHeight(420.f) {}
        SLATE_ATTRIBUTE(float, DiagramHeight)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override;

private:
    void RefreshSelection();
    void ShowPart(int32 Index);
    TSharedPtr<SPoseDollBodyDiagram> Diagram;
    TSharedPtr<SWrapBox> SelectedLabels;
    FString SelectionKey;
    FText PartName, PartDescription;
    float AvailableWidth = 560.f;
    int32 SelectedCount = 0;
    TAttribute<float> DiagramHeight;
};
}

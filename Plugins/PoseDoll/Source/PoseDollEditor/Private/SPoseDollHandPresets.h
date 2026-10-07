#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PoseDollHandPresets.h"
class SWrapBox;
struct FSlateDynamicImageBrush;
namespace PoseDoll
{
class SPoseDollHandPresets : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPoseDollHandPresets) : _Linear(false) {}
        SLATE_ARGUMENT(bool,Linear)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    TSharedRef<SWidget> CountFilter(int32 Index,const FString& Label);
    TSharedRef<SWidget> FingerFilter(int32 Index,const FString& Label);
    TSharedRef<SWidget> FilterButton(const FString& Label,TFunction<bool()> Active,TFunction<void()> Pick);
    void Rebuild();
    const FSlateBrush* Brush(const FHandPreset& Preset,EHandSide ForSide);
    FText StatusText() const;
    TSharedPtr<SWrapBox> Grid;
    TMap<FString,TSharedPtr<FSlateDynamicImageBrush>> Images;
    TSharedPtr<FHandPreset> Selected;
    EHandSide Side=EHandSide::Left;
    int32 Counts[3]={-1,-1,-1};
    TCHAR Fingers[5]={0,0,0,0,0};
    int32 Visible=0;
    bool Linear=false;
    FString Error;
};
}

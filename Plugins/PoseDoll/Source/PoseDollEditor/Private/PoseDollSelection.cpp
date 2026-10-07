#include "PoseDollSession.h"
namespace PoseDoll
{
const TArray<TPair<FString,FString>>& FSession::PartOptions()
{
    static const TArray<TPair<FString,FString>> Parts={
        {TEXT("pelvis"),TEXT("骨盆")},{TEXT("waist"),TEXT("腰部")},{TEXT("chest"),TEXT("胸部")},{TEXT("head"),TEXT("头颈")},
        {TEXT("clavicle_l"),TEXT("左肩带")},{TEXT("upperarm_l"),TEXT("左上臂")},{TEXT("lowerarm_l"),TEXT("左前臂")},{TEXT("hand_l"),TEXT("左手腕")},
        {TEXT("clavicle_r"),TEXT("右肩带")},{TEXT("upperarm_r"),TEXT("右上臂")},{TEXT("lowerarm_r"),TEXT("右前臂")},{TEXT("hand_r"),TEXT("右手腕")},
        {TEXT("thigh_l"),TEXT("左大腿")},{TEXT("calf_l"),TEXT("左小腿")},{TEXT("foot_l"),TEXT("左脚踝")},{TEXT("ball_l"),TEXT("左前脚掌")},
        {TEXT("thigh_r"),TEXT("右大腿")},{TEXT("calf_r"),TEXT("右小腿")},{TEXT("foot_r"),TEXT("右脚踝")},{TEXT("ball_r"),TEXT("右前脚掌")}
    };
    return Parts;
}
FString FSession::MaskLabel(const FString& Value)
{
    static const TMap<FString,FString> Labels={
        {TEXT("FullBody"),TEXT("全身")},{TEXT("UpperBody"),TEXT("上半身")},{TEXT("LowerBody"),TEXT("下半身（不含骨盆）")},
        {TEXT("arm_l"),TEXT("左臂")},{TEXT("arm_r"),TEXT("右臂")},{TEXT("leg_l"),TEXT("左腿")},{TEXT("leg_r"),TEXT("右腿")},{TEXT("Custom"),TEXT("自定义")}
    };
    const auto* Label=Labels.Find(Value);return Label?*Label:Value;
}
bool FSession::SetCustomParts(const TSet<FString>& Parts)
{
    for(const FString& Part:Parts)
        if(!PartOptions().ContainsByPredicate([&](const auto& P){return P.Key==Part;}))
        {Error=TEXT("Unknown capture part: ")+Part;return false;}
    InvalidateSnapshotContext();CustomParts=Parts;Mask=TEXT("Custom");bValid=false;Error.Empty();return true;
}
void FSession::TogglePart(const FString& Part,bool Enabled)
{
    TSet<FString> Parts;
    if(Adapter)for(const auto& M:Adapter->GetMapping())if(IsMasked(M))Parts.Add(M.Semantic);
    if(Enabled)Parts.Add(Part);else Parts.Remove(Part);
    SetCustomParts(Parts);
}
FString FSession::SelectionLabel() const
{
    if(Mask!=TEXT("Custom"))return MaskLabel(Mask);
    if(CustomParts.Num()==1)
        for(const auto& P:PartOptions())if(CustomParts.Contains(P.Key))return P.Value;
    return FString::Printf(TEXT("所选 %d 个部位"),CustomParts.Num());
}
}

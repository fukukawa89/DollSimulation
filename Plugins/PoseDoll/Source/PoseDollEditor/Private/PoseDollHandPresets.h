#pragma once
#include "CoreMinimal.h"
#include "PoseDollPoseEditing.h"

namespace PoseDoll
{
enum class EHandSide : uint8 { Left, Right, Both };

// All values are complete local Control Rig transforms, not additive deltas.
struct FHandPreset
{
    FString Id;
    FString States; // Thumb, index, middle, ring, little; E/B/C.
    TMap<FName,FTransform> Left,Right;
    int32 Count(TCHAR State) const;
    FString ImagePath(EHandSide Side) const;
};

class FHandPresetLibrary
{
public:
    static FHandPresetLibrary& Get();
    bool Load(FString& Error);
    void Reset(){Presets.Reset();}
    const TArray<TSharedPtr<FHandPreset>>& All() const { return Presets; }
    const FHandPreset* Find(const FString& Id) const;
    static FString Root();
    static const TArray<FString>& Fingers();
    static TArray<FName> ControlNames(EHandSide Side);
    static bool IsHandBone(FName Name,EHandSide Side);
    static TMap<FName,FTransform> ReadBones(UControlRig* Rig);
    static bool BuildEdit(UControlRig* Rig,const FHandPreset& Preset,EHandSide Side,FEditPose& Out,FString& Error);
    static bool VerifyBones(UControlRig* Rig,const TMap<FName,FTransform>& Expected,FString& Error);
    bool RenderThumbnails(UControlRig* Reference,USkeletalMesh* Mesh,FString& Error,int32 Start=0,int32 Count=4);
    FString Report() const;
private:
    TArray<TSharedPtr<FHandPreset>> Presets;
};
}

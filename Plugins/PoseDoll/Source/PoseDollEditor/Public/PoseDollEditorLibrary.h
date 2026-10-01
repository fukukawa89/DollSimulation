#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseDollEditorLibrary.generated.h"
class ULevelSequence;
class UControlRig;
class USkeletalMeshComponent;

UCLASS()
class POSEDOLLEDITOR_API UPoseDollEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString InspectRig(const FString& MeshPath, const FString& RigPath);
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString RunTransportProbe(float Seconds = 5.0f);
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString TestRigFixtures();
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString SessionCommand(const FString& Action, const FString& Argument = TEXT(""));
    // O22 file input: solve only. Sequencer writing remains an explicit editor action.
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString SolveMeasuredPose22(const FString& PayloadFile, const FString& TargetProfileFile, bool AllowSyntheticForTesting = false);
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static FString CaptureMeasuredPose22(ULevelSequence* Sequence, UControlRig* ControlRig, int32 Frame, const FString& PayloadFile, const FString& TargetProfileFile, bool AllowSyntheticForTesting = false);
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static bool BindTarget(ULevelSequence* Sequence, USkeletalMeshComponent* Component);
};

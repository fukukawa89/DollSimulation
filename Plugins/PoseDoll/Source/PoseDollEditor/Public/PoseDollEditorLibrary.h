#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseDollEditorLibrary.generated.h"
class ULevelSequence;
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
    UFUNCTION(BlueprintCallable, Category="PoseDoll")
    static bool BindTarget(ULevelSequence* Sequence, USkeletalMeshComponent* Component);
};

#pragma once
#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "PoseDollToolset.generated.h"

/** Inspect and test PoseDoll's native business services. Never used for high frequency samples. */
UCLASS(BlueprintType)
class UPoseDollToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Return connection, one-shot capture, binding and measured performance. */
    UFUNCTION(meta=(AICallable),Category="PoseDoll")
    static FString GetPoseDollStatus();
    /** Load a bundled raw sensor fixture into the transient preview. Does not create keys. */
    UFUNCTION(meta=(AICallable),Category="PoseDoll")
    static FString LoadPoseDollFixture(const FString& Filename);
    /** Run native numerical fixtures against the curated Control Rig; return a JSON report. */
    UFUNCTION(meta=(AICallable),Category="PoseDoll")
    static FString ValidatePoseDollRig();
    /** Execute a low frequency, explicit session operation; the native service validates the action and target. */
    UFUNCTION(meta=(AICallable),Category="PoseDoll")
    static FString PoseDollSession(const FString& Action,const FString& Argument=TEXT(""));
};

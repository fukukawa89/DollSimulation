#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PoseDoll
{
// Mathematical column-vector, right-handed, metre matrices. Never reinterpret as FMatrix.
struct POSEDOLLCORE_API FMatrix44
{
    double M[4][4] = {};
    static FMatrix44 Identity();
    static FMatrix44 Rotation(const FVector3d& Axis, double Angle);
    FMatrix44 operator*(const FMatrix44& Other) const;
    FTransform ToUnreal() const;
};

struct FAxis
{
    FString Id;
    double Min = 0, Max = 0, Zero = 0, Sign = 1, Scale = 1, Period = 2 * UE_DOUBLE_PI;
};
struct FNode
{
    FString Id;
    int32 Parent = INDEX_NONE, AxisIndex = INDEX_NONE;
    FVector3d Axis = FVector3d::ZeroVector;
    FMatrix44 Before, After;
};
struct POSEDOLLCORE_API FProfile
{
    FString Id, Hash, CalibrationId, CalibrationHash, CapabilityId;
    TArray<FAxis> Axes;
    TArray<FNode> Nodes;
    TMap<FString, int32> NodeIndices, AxisIndices, Segments;
    TMap<int32, double> FixedAxes;
    bool Load(const FString& ProfilesDirectory, FString& Error);
    bool Forward(const TArray<double>& Angles, TArray<FMatrix44>& Output, FString& Error) const;
};
struct FSample
{
    uint64 Sequence = 0, SenderMicros = 0;
    TArray<double> Raw;
    TArray<FString> Status;
    double ReceivedSeconds = 0;
};
struct FIdentity
{
    FString Device, Session, Capability;
};
POSEDOLLCORE_API bool ReadJson(const FString& Text, TSharedPtr<FJsonObject>& Out, FString& Error);
POSEDOLLCORE_API bool LoadJson(const FString& File, TSharedPtr<FJsonObject>& Out, FString& Error);
POSEDOLLCORE_API FString JsonString(const TSharedRef<FJsonObject>& Object);
POSEDOLLCORE_API FString Sha256Text(const FString& Text);
POSEDOLLCORE_API bool Handshake(const FProfile& Profile, const FJsonObject& Hello, FIdentity& Out, FString& Error);
POSEDOLLCORE_API bool ParseSample(const FProfile& Profile, const FIdentity& Identity, const FJsonObject& Object, FSample& Out, FString& Error);

class POSEDOLLCORE_API FDecoder
{
public:
    double FilterSeconds = 0, MaximumSpeed = 40;
    void Reset();
    bool Decode(const FProfile& Profile, const FIdentity& Identity, const FSample& Sample, TArray<double>& Angles, FString& Error);
private:
    bool bInitialized = false;
    uint64 Sequence = 0, Micros = 0;
    TArray<double> PreviousRaw, Unwrapped, Filtered;
};
}

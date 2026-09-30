#pragma once
#include "PoseDollCore.h"

namespace PoseDoll
{
// PDS1 is a separate request protocol. uint64 wire values are decimal strings.
struct FStaticIdentity
{
    FIdentity Source;
    TArray<FString> Boots; // N1..N6, optionally D3,D4; never extra pose slots.
    FString SourceKind;
};
struct FStaticMessage
{
    FString Type, CaptureId;
    uint64 RequestStart=0, Start=0, End=0, ScanId=0;
    uint32 Duration=0;
    TArray<FString> Boots;
    FSample Sample;
};
POSEDOLLCORE_API TSharedRef<FJsonObject> StaticEnvelope(const TSharedRef<FJsonObject>& Payload);
POSEDOLLCORE_API bool UnwrapStatic(const FJsonObject& Envelope,TSharedPtr<FJsonObject>& Payload,FString& Error);
POSEDOLLCORE_API bool StaticHello(const FProfile& Profile,const FJsonObject& Payload,FStaticIdentity& Identity,FString& Error);
POSEDOLLCORE_API bool ParseStatic(const FProfile& Profile,const FStaticIdentity& Identity,const FJsonObject& Payload,FStaticMessage& Message,FString& Error);
POSEDOLLCORE_API TSharedRef<FJsonObject> StaticCommand(const FProfile& Profile,const FStaticIdentity& Identity,const FString& CaptureId,const FString& Type);
POSEDOLLCORE_API bool DecodeAbsolute(const FProfile& Profile,const FStaticMessage& Message,TArray<double>& Angles,FString& Error);
// Used only after CRC, schema and session identity validation. Reset on reconnect.
enum class EStaticRoute { Active, Retired, Unexpected };
class POSEDOLLCORE_API FStaticRouter
{
public:
    static constexpr int32 MaxRetired = 32;
    static constexpr double RetiredSeconds = 30.0;
    void Reset();
    bool Begin(const FString& Id, double Now);
    void Retire(const FString& Id, double Now);
    EStaticRoute Route(const FString& Id, double Now);
    int32 RetiredCount() const { return Retired.Num(); }
private:
    struct FRetired { FString Id; double Expires; };
    void Prune(double Now);
    FString Active;
    TArray<FRetired> Retired;
};
class POSEDOLLCORE_API FStaticWindow
{
public:
    void Begin(const FString& Id,double WallSeconds);
    bool Push(const FProfile& Profile,const FStaticMessage& Message,FString& Error);
    bool CheckDeadline(double Now,FString& Error);
    void Cancel();
    bool Pending() const {return State==TEXT("Requested") || State==TEXT("WaitStable");}
    FString State=TEXT("Idle"), CaptureId;
    FStaticMessage Final;
    double Started=0, PeakDegrees=0, DriftDegreesPerSecond=0, AcceptedSeconds=0;
    uint64 StableMicros=0;
private:
    bool Reject(const FString& Reason,FString& Error);
    uint64 RequestStart=0,LastId=0,LastStart=0,LastEnd=0;
    bool bAck=false;
    TArray<FString> Boots;
    TArray<FStaticMessage> Window;
    TArray<TArray<double>> Angles;
};
}

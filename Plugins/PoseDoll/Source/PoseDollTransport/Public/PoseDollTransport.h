#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "PoseDollCore.h"

namespace PoseDoll
{
struct FTransportSnapshot
{
    FString State=TEXT("Disconnected"), Error;
    FIdentity Identity;
    FSample Latest;
    uint64 Generation=0, Received=0, Rejected=0;
    bool bConnected=false, bHasSample=false;
};

class POSEDOLLTRANSPORT_API FTcpSource final : public FRunnable
{
public:
    explicit FTcpSource(const FProfile& InProfile);
    ~FTcpSource();
    void Start(uint16 InPort=39177);
    void Stop() override;
    uint32 Run() override;
    FTransportSnapshot Snapshot() const;
private:
    void SetError(const FString& Error);
    FProfile Profile;
    uint16 Port=39177;
    FThreadSafeBool bStop=false;
    TUniquePtr<FRunnableThread> Thread;
    mutable FCriticalSection Mutex;
    FTransportSnapshot State;
};
}

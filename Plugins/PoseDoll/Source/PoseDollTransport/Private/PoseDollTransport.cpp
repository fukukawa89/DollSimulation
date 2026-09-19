#include "PoseDollTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

namespace PoseDoll
{
// Reject overlong sequences, invalid continuation bytes, surrogate code points and > U+10FFFF.
static bool ValidUTF8(const uint8* P,int32 Size)
{
    for (int32 I=0; I<Size;)
    {
        uint32 C=P[I++]; int32 N=0; uint32 Min=0;
        if (C<0x80) continue;
        if (C>=0xC2 && C<=0xDF) {N=1;C&=31;Min=0x80;}
        else if (C>=0xE0 && C<=0xEF) {N=2;C&=15;Min=0x800;}
        else if (C>=0xF0 && C<=0xF4) {N=3;C&=7;Min=0x10000;}
        else return false;
        if (I+N>Size) return false;
        while (N--) {const uint8 X=P[I++];if ((X&0xC0)!=0x80) return false;C=(C<<6)|(X&63);}
        if (C<Min || C>0x10FFFF || (C>=0xD800 && C<=0xDFFF)) return false;
    }
    return true;
}
static bool SendObject(FSocket* Socket,const TSharedRef<FJsonObject>& O)
{
    FTCHARToUTF8 UTF8(*JsonString(O)); const uint32 N=UTF8.Length();
    TArray<uint8> Bytes; Bytes.Add(uint8(N>>24));Bytes.Add(uint8(N>>16));Bytes.Add(uint8(N>>8));Bytes.Add(uint8(N));Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()),N);
    int32 Offset=0;const double End=FPlatformTime::Seconds()+.5;
    while (Offset<Bytes.Num() && FPlatformTime::Seconds()<End)
    {
        int32 Sent=0;
        if (Socket->Send(Bytes.GetData()+Offset,Bytes.Num()-Offset,Sent)) Offset+=Sent;
        else if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode()!=SE_EWOULDBLOCK) return false;
        if (Offset<Bytes.Num()) Socket->Wait(ESocketWaitConditions::WaitForWrite,FTimespan::FromMilliseconds(10));
    }
    return Offset==Bytes.Num();
}
FTcpSource::FTcpSource(const FProfile& P):Profile(P) {}
FTcpSource::~FTcpSource() { Stop(); if (Thread) {Thread->WaitForCompletion();Thread.Reset();} }
void FTcpSource::Start(uint16 InPort)
{
    if (Thread) return;
    Port=InPort;bStop=false;Thread.Reset(FRunnableThread::Create(this,TEXT("PoseDollSensorTCP")));
}
void FTcpSource::Stop() { bStop=true; }
FTransportSnapshot FTcpSource::Snapshot() const { FScopeLock Guard(&Mutex);return State; }
void FTcpSource::SetError(const FString& Error) {FScopeLock Guard(&Mutex);State.Error=Error;++State.Rejected;}
uint32 FTcpSource::Run()
{
    ISocketSubsystem* Subsystem=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    while (!bStop)
    {
        {FScopeLock Guard(&Mutex);State.State=TEXT("Connecting");State.bConnected=false;State.bHasSample=false;}
        FSocket* Socket=Subsystem->CreateSocket(NAME_Stream,TEXT("PoseDoll loopback"),false);
        if (!Socket) {SetError(TEXT("Socket creation failed"));return 1;}
        Socket->SetNonBlocking(true);Socket->SetNoDelay(true);
        TSharedRef<FInternetAddr> Address=Subsystem->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(Port);
        Socket->Connect(*Address);
        const double Deadline=FPlatformTime::Seconds()+2;
        bool Connected=false;
        while (!bStop && FPlatformTime::Seconds()<Deadline)
        {
            if (Socket->Wait(ESocketWaitConditions::WaitForWrite,FTimespan::FromMilliseconds(50)) && Socket->GetConnectionState()==SCS_Connected) {Connected=true;break;}
        }
        TArray<uint8> Buffer; FIdentity ID; bool Welcomed=false,Alive=Connected;
        uint64 LastSequence=0,LastTime=0;bool HaveSequence=false;
        double RateWindow=FPlatformTime::Seconds(),PartialSince=RateWindow;int32 MessageCount=0;
        while (!bStop && Alive)
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForRead,FTimespan::FromMilliseconds(20)))
            {
                if ((!Welcomed || Buffer.Num()>0) && FPlatformTime::Seconds()-PartialSince>3) {SetError(TEXT("Handshake/partial frame timeout"));break;}
                continue;
            }
            uint8 Chunk[16384];int32 Read=0;
            if (!Socket->Recv(Chunk,sizeof(Chunk),Read) || Read<=0) break;
            if (Buffer.IsEmpty()) PartialSince=FPlatformTime::Seconds();
            Buffer.Append(Chunk,Read);
            if (Buffer.Num()>131080) {SetError(TEXT("Receive buffer budget"));break;}
            while (Buffer.Num()>=4)
            {
                const uint32 N=(uint32(Buffer[0])<<24)|(uint32(Buffer[1])<<16)|(uint32(Buffer[2])<<8)|Buffer[3];
                if (N==0 || N>65536) {SetError(TEXT("Invalid frame length"));Alive=false;break;}
                if (uint32(Buffer.Num())<N+4) break;
                const double Now=FPlatformTime::Seconds();
                if (Now-RateWindow>=1) {RateWindow=Now;MessageCount=0;}
                if (++MessageCount>480) {SetError(TEXT("Message rate budget"));Alive=false;break;}
                if (!ValidUTF8(Buffer.GetData()+4,N)) {SetError(TEXT("Invalid UTF-8"));Alive=false;break;}
                FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()+4),N);
                TSharedPtr<FJsonObject> Object;FString Error;
                const bool Parsed=ReadJson(FString(Text.Length(),Text.Get()),Object,Error);
                Buffer.RemoveAt(0,N+4,EAllowShrinking::No);
                if (!Parsed) {SetError(Error);Alive=false;break;}
                if (!Welcomed)
                {
                    if (!Handshake(Profile,*Object,ID,Error))
                    {
                        auto Reject=MakeShared<FJsonObject>();Reject->SetStringField(TEXT("type"),TEXT("reject"));Reject->SetStringField(TEXT("code"),Error);SendObject(Socket,Reject);SetError(Error);Alive=false;break;
                    }
                    auto Welcome=MakeShared<FJsonObject>();Welcome->SetStringField(TEXT("type"),TEXT("welcome"));Welcome->SetStringField(TEXT("protocol"),TEXT("posedoll.sensor/1"));Welcome->SetStringField(TEXT("session_id"),ID.Session);Welcome->SetBoolField(TEXT("accepted"),true);Welcome->SetStringField(TEXT("receiver"),TEXT("PoseDoll/UE5.8"));Welcome->SetNumberField(TEXT("max_frame_bytes"),65536);
                    if (!SendObject(Socket,Welcome)) {Alive=false;break;}
                    Welcomed=true;
                    {FScopeLock Guard(&Mutex);State.Identity=ID;State.bConnected=true;State.State=TEXT("Ready");State.Error.Reset();++State.Generation;}
                    continue;
                }
                FSample Sample;
                if (!ParseSample(Profile,ID,*Object,Sample,Error)) {SetError(Error);Alive=false;break;}
                if (HaveSequence && (Sample.Sequence<=LastSequence || Sample.SenderMicros<=LastTime)) {SetError(TEXT("Non-monotonic sample"));continue;}
                HaveSequence=true;LastSequence=Sample.Sequence;LastTime=Sample.SenderMicros;Sample.ReceivedSeconds=Now;
                {FScopeLock Guard(&Mutex);State.Latest=MoveTemp(Sample);State.bHasSample=true;++State.Received;}
            }
        }
        Socket->Close();Subsystem->DestroySocket(Socket);
        {FScopeLock Guard(&Mutex);State.bConnected=false;State.bHasSample=false;State.State=TEXT("Disconnected");}
        for (int32 I=0; I<10 && !bStop; ++I) FPlatformProcess::Sleep(.05f);
    }
    return 0;
}
}

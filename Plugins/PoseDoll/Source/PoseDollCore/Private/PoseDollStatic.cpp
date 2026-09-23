#include "PoseDollStatic.h"
#include "Misc/Crc.h"
namespace PoseDoll
{
static bool Bad(FString& E,const FString& M){E=M;return false;}
static bool U64(const FJsonObject& O,const TCHAR* Key,uint64& V)
{
    FString S;if(!O.TryGetStringField(Key,S)||S.IsEmpty()||S.Len()>20||(S.Len()>1&&S[0]=='0'))return false;
    V=0;for(TCHAR C:S){if(C<'0'||C>'9'||V>(MAX_uint64-uint64(C-'0'))/10)return false;V=V*10+C-'0';}return true;
}
static bool Strings(const FJsonObject& O,const TCHAR* Key,TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O.TryGetArrayField(Key,A))return false;
    Out.Reset();for(const auto& V:*A){FString S;if(!V->TryGetString(S)||S.IsEmpty()||S.Len()>128)return false;Out.Add(S);}return true;
}
static bool Equals(const FJsonObject& O,const TCHAR* K,const FString& Want)
{FString S;return O.TryGetStringField(K,S)&&S==Want;}
TSharedRef<FJsonObject> StaticEnvelope(const TSharedRef<FJsonObject>& Payload)
{
    auto O=MakeShared<FJsonObject>();const FString Text=JsonString(Payload);FTCHARToUTF8 Bytes(*Text);
    O->SetStringField(TEXT("type"),TEXT("pds1"));O->SetStringField(TEXT("payload"),Text);
    O->SetStringField(TEXT("crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Bytes.Get(),Bytes.Length())));return O;
}
bool UnwrapStatic(const FJsonObject& O,TSharedPtr<FJsonObject>& P,FString& E)
{
    FString Text,CRC;if(O.Values.Num()!=3||!Equals(O,TEXT("type"),TEXT("pds1"))||!O.TryGetStringField(TEXT("payload"),Text)||!O.TryGetStringField(TEXT("crc32"),CRC))return Bad(E,TEXT("PDS1 envelope fields"));
    FTCHARToUTF8 Bytes(*Text);if(Bytes.Length()>32768||CRC!=FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Bytes.Get(),Bytes.Length())))return Bad(E,TEXT("PDS1 CRC/length"));
    return ReadJson(Text,P,E)&&Equals(*P,TEXT("protocol"),TEXT("PDS1/1"));
}
bool StaticHello(const FProfile& P,const FJsonObject& O,FStaticIdentity& ID,FString& E)
{
    TArray<FString> Order;
    if(O.Values.Num()!=9||!Equals(O,TEXT("type"),TEXT("hello"))||!Equals(O,TEXT("protocol"),TEXT("PDS1/1"))||!Equals(O,TEXT("profile_sha256"),P.Hash)||!Equals(O,TEXT("calibration_sha256"),P.CalibrationHash)||!Strings(O,TEXT("axis_order"),Order)||Order.Num()!=44)return Bad(E,TEXT("PDS1 hello/profile"));
    for(int32 I=0;I<44;++I)if(Order[I]!=P.Axes[I].Id)return Bad(E,TEXT("PDS1 axis order"));
    if(!O.TryGetStringField(TEXT("device_id"),ID.Source.Device)||!O.TryGetStringField(TEXT("gateway_boot"),ID.Source.Session)||ID.Source.Device.IsEmpty()||ID.Source.Session.IsEmpty()||ID.Source.Device.Len()>128||ID.Source.Session.Len()>128||!Strings(O,TEXT("source_boots"),ID.Boots)||(ID.Boots.Num()!=6&&ID.Boots.Num()!=8)||!O.TryGetStringField(TEXT("source_kind"),ID.SourceKind)||(ID.SourceKind!=TEXT("simulated")&&ID.SourceKind!=TEXT("hardware")))return Bad(E,TEXT("PDS1 source identity"));
    ID.Source.Capability=TEXT("pds1_full41_root3");return true;
}
TSharedRef<FJsonObject> StaticCommand(const FProfile& P,const FStaticIdentity& ID,const FString& Capture,const FString& Type)
{
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("protocol"),TEXT("PDS1/1"));O->SetStringField(TEXT("type"),Type);O->SetStringField(TEXT("device_id"),ID.Source.Device);O->SetStringField(TEXT("gateway_boot"),ID.Source.Session);O->SetStringField(TEXT("profile_sha256"),P.Hash);O->SetStringField(TEXT("calibration_sha256"),P.CalibrationHash);O->SetStringField(TEXT("capture_id"),Capture);return O;
}
bool ParseStatic(const FProfile& P,const FStaticIdentity& ID,const FJsonObject& O,FStaticMessage& M,FString& E)
{
    if(!Equals(O,TEXT("protocol"),TEXT("PDS1/1"))||!Equals(O,TEXT("device_id"),ID.Source.Device)||!Equals(O,TEXT("gateway_boot"),ID.Source.Session)||!Equals(O,TEXT("profile_sha256"),P.Hash)||!Equals(O,TEXT("calibration_sha256"),P.CalibrationHash)||!O.TryGetStringField(TEXT("capture_id"),M.CaptureId)||M.CaptureId.IsEmpty()||M.CaptureId.Len()>128||!O.TryGetStringField(TEXT("type"),M.Type)||!U64(O,TEXT("request_start_us"),M.RequestStart)||!Strings(O,TEXT("source_boots"),M.Boots)||M.Boots!=ID.Boots)return Bad(E,TEXT("PDS1 request/source/profile changed"));
    if(M.Type==TEXT("accepted"))return O.Values.Num()==9||Bad(E,TEXT("PDS1 ack fields"));
    if(M.Type!=TEXT("scan")||O.Values.Num()!=15||!U64(O,TEXT("scan_id"),M.ScanId)||M.ScanId==0||!U64(O,TEXT("start_us"),M.Start)||!U64(O,TEXT("end_us"),M.End))return Bad(E,TEXT("PDS1 scan fields"));
    double D=0;if(!O.TryGetNumberField(TEXT("duration_us"),D)||!FMath::IsFinite(D)||D<1||D>MAX_uint32||FMath::FloorToDouble(D)!=D||M.Start<M.RequestStart||M.End<=M.Start||M.End-M.Start!=uint64(D))return Bad(E,TEXT("PDS1 duration/range"));M.Duration=uint32(D);
    const TArray<TSharedPtr<FJsonValue>> *Raw=nullptr,*Status=nullptr;
    if(!O.TryGetArrayField(TEXT("raw_angles_rad"),Raw)||!O.TryGetArrayField(TEXT("axis_status"),Status)||Raw->Num()!=44||Status->Num()!=44)return Bad(E,TEXT("PDS1 complete 44-slot pose required"));
    M.Sample.Raw.SetNumZeroed(44);M.Sample.Status.SetNum(44);M.Sample.Sequence=M.ScanId;M.Sample.SenderMicros=M.End;
    for(int32 I=0;I<44;++I)
    {
        FString S;if(!(*Status)[I]->TryGetString(S))return Bad(E,TEXT("PDS1 invalid status"));M.Sample.Status[I]=S;
        if(I<3){if(S!=TEXT("fixed")||(*Raw)[I]->Type!=EJson::Null||P.Axes[I].Id!=TArray<FString>({TEXT("pelvis.yaw"),TEXT("pelvis.pitch"),TEXT("pelvis.roll")})[I])return Bad(E,TEXT("PDS1 pelvis must be fixed zero reference"));}
        else if(S!=TEXT("valid")||(*Raw)[I]->Type!=EJson::Number||!(*Raw)[I]->TryGetNumber(M.Sample.Raw[I])||!FMath::IsFinite(M.Sample.Raw[I])||M.Sample.Raw[I]<0||M.Sample.Raw[I]>=P.Axes[I].Period)return Bad(E,TEXT("PDS1 missing/fault/invalid measurement"));
    }
    return true;
}
bool DecodeAbsolute(const FProfile& P,const FStaticMessage& M,TArray<double>& Q,FString& E)
{
    // A fresh decoder applies the existing calibration and unique hard-limit branch.
    // It has no previous pose, speed heuristic or filter state to guess a turn from.
    FProfile StaticProfile=P;StaticProfile.CapabilityId=TEXT("pds1_full41_root3");StaticProfile.FixedAxes.Reset();for(int32 I=0;I<3;++I)StaticProfile.FixedAxes.Add(I,0);
    FDecoder D;FIdentity ID;ID.Capability=StaticProfile.CapabilityId;return D.Decode(StaticProfile,ID,M.Sample,Q,E);
}
void FStaticWindow::Begin(const FString& Id,double Wall)
{*this=FStaticWindow();CaptureId=Id;Started=Wall;State=TEXT("Requested");}
void FStaticWindow::Cancel(){Window.Reset();Angles.Reset();State=TEXT("Cancelled");}
bool FStaticWindow::Reject(const FString& Why,FString& E)
{Window.Reset();Angles.Reset();State=TEXT("Fault");return Bad(E,Why);}
bool FStaticWindow::CheckDeadline(double Now,FString& E)
{if(Pending()&&(Now<Started||Now-Started>3)){Window.Reset();Angles.Reset();State=TEXT("TimedOut");return Bad(E,TEXT("Static capture timed out after 3 s"));}return true;}
bool FStaticWindow::Push(const FProfile& P,const FStaticMessage& M,FString& E)
{
    if(!Pending())return Bad(E,TEXT("No pending static request"));
    if(!CheckDeadline(M.Sample.ReceivedSeconds,E))return false;
    if(M.CaptureId!=CaptureId)return Reject(TEXT("Wrong static capture ID"),E);
    if(M.Type==TEXT("accepted"))
    {
        if(bAck && (RequestStart!=M.RequestStart||Boots!=M.Boots))return Reject(TEXT("Conflicting request acknowledgement"),E);
        bAck=true;RequestStart=M.RequestStart;Boots=M.Boots;return true;
    }
    if(!bAck||M.RequestStart!=RequestStart||M.Boots!=Boots)return Reject(TEXT("Unacknowledged request or changed boot"),E);
    if(M.Duration>100000||M.ScanId!=LastId+1||M.Start<LastEnd||(LastId&&M.Start<=LastStart)||M.Start<RequestStart||M.End<=M.Start)return Reject(TEXT("Incomplete, old, overlapping or overlong scan"),E);
    // Request send is a conservative cross-clock lower bound. No subtraction of
    // unrelated absolute clocks. 5 ms is a clock-rate/timer tolerance, not RTT credit.
    const double Elapsed=(M.Sample.ReceivedSeconds-Started)*1e6;
    if(double(M.End-RequestStart)>Elapsed+5000)return Reject(TEXT("Future source interval"),E);
    if(LastId&&M.Start-LastStart>150000){Window.Reset();Angles.Reset();}
    LastId=M.ScanId;LastStart=M.Start;LastEnd=M.End;
    TArray<double> Q;if(!DecodeAbsolute(P,M,Q,E)){const FString Why=E;return Reject(Why,E);}
    Window.Add(M);Angles.Add(MoveTemp(Q));State=TEXT("WaitStable");
    // Retain the shortest complete window that still covers 500 ms.
    while(Window.Num()>6 && Window.Last().Start>=Window[1].End && Window.Last().Start-Window[1].End>=500000){Window.RemoveAt(0);Angles.RemoveAt(0);}
    if(Window.Num()>32)return Reject(TEXT("Static observation buffer limit"),E);
    if(Window.Num()<6||Window.Last().Start-Window[0].End<500000)return true;
    double MeanT=0;TArray<double> T;
    for(const auto& W:Window){T.Add((double(W.Start-Window[0].Start)+double(W.End-Window[0].Start))*.5e-6);MeanT+=T.Last();}MeanT/=T.Num();
    double Den=0;for(double X:T)Den+=FMath::Square(X-MeanT);PeakDegrees=0;DriftDegreesPerSecond=0;
    for(int32 Axis=3;Axis<44;++Axis)
    {
        double Lo=Angles[0][Axis],Hi=Lo,Mean=0,Num=0;
        for(const auto& A:Angles){Lo=FMath::Min(Lo,A[Axis]);Hi=FMath::Max(Hi,A[Axis]);Mean+=A[Axis];}Mean/=Angles.Num();
        for(int32 I=0;I<T.Num();++I)Num+=(T[I]-MeanT)*(Angles[I][Axis]-Mean);
        PeakDegrees=FMath::Max(PeakDegrees,FMath::RadiansToDegrees(Hi-Lo));DriftDegreesPerSecond=FMath::Max(DriftDegreesPerSecond,FMath::RadiansToDegrees(FMath::Abs(Num/Den)));
    }
    if(PeakDegrees>.30+1e-9||DriftDegreesPerSecond>.20+1e-9)return true;
    if(Elapsed-double(M.Start-RequestStart)>200000)return Reject(TEXT("Final static scan is stale"),E);
    Final=M;StableMicros=M.Start-Window[0].End;AcceptedSeconds=M.Sample.ReceivedSeconds;State=TEXT("SnapshotReady");return true;
}
}

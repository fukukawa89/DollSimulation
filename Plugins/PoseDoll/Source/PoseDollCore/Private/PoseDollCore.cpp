#include "PoseDollCore.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace PoseDoll
{
static bool Fail(FString& Error, const FString& Message) { Error = Message; return false; }
FString Sha256Text(const FString& Text)
{
    FTCHARToUTF8 Bytes(*Text);uint8 Digest[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Digest);return BytesToHex(Digest,SHA256_DIGEST_LENGTH).ToLower();
}
static bool FiniteJson(const TSharedPtr<FJsonValue>& V)
{
    if (!V.IsValid()) return false;
    if (V->Type==EJson::Number) return FMath::IsFinite(V->AsNumber());
    if (V->Type==EJson::Array) { for (const auto& X:V->AsArray()) if (!FiniteJson(X)) return false; }
    if (V->Type==EJson::Object) { for (const auto& X:V->AsObject()->Values) if (!FiniteJson(X.Value)) return false; }
    return true;
}
static bool Number(const TSharedPtr<FJsonValue>& V, double& Out)
{
    return V.IsValid() && V->Type == EJson::Number && V->TryGetNumber(Out) && FMath::IsFinite(Out);
}
static bool Numbers(const FJsonObject& O, const TCHAR* Name, int32 Count, TArray<double>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!O.TryGetArrayField(Name, A) || A->Num() != Count) return false;
    Out.Reset();
    for (const auto& V : *A) { double N; if (!Number(V, N)) return false; Out.Add(N); }
    return true;
}
static bool Rigid(const TSharedPtr<FJsonValue>& V, FMatrix44& Out)
{
    if (!V.IsValid() || V->Type != EJson::Object) return false;
    TArray<double> Q, P;
    const FJsonObject& O = *V->AsObject();
    if (!Numbers(O, TEXT("rotation_xyzw"), 4, Q) || !Numbers(O, TEXT("translation_m"), 3, P)) return false;
    const double X=Q[0], Y=Q[1], Z=Q[2], W=Q[3];
    if (FMath::Abs(X*X+Y*Y+Z*Z+W*W-1) > 1e-7) return false;
    Out = FMatrix44::Identity();
    Out.M[0][0]=1-2*(Y*Y+Z*Z); Out.M[0][1]=2*(X*Y-Z*W); Out.M[0][2]=2*(X*Z+Y*W);
    Out.M[1][0]=2*(X*Y+Z*W); Out.M[1][1]=1-2*(X*X+Z*Z); Out.M[1][2]=2*(Y*Z-X*W);
    Out.M[2][0]=2*(X*Z-Y*W); Out.M[2][1]=2*(Y*Z+X*W); Out.M[2][2]=1-2*(X*X+Y*Y);
    for (int32 I=0; I<3; ++I) Out.M[I][3]=P[I];
    return true;
}
FMatrix44 FMatrix44::Identity()
{
    FMatrix44 R; for (int32 I=0; I<4; ++I) R.M[I][I]=1; return R;
}
FMatrix44 FMatrix44::Rotation(const FVector3d& A, double Angle)
{
    const double C=FMath::Cos(Angle), S=FMath::Sin(Angle), T=1-C;
    FMatrix44 R=Identity();
    for (int32 I=0; I<3; ++I) for (int32 J=0; J<3; ++J) R.M[I][J]=T*A[I]*A[J]+(I==J ? C : 0);
    R.M[0][1]-=S*A.Z; R.M[0][2]+=S*A.Y;
    R.M[1][0]+=S*A.Z; R.M[1][2]-=S*A.X;
    R.M[2][0]-=S*A.Y; R.M[2][1]+=S*A.X;
    return R;
}
FMatrix44 FMatrix44::operator*(const FMatrix44& B) const
{
    FMatrix44 R;
    for (int32 I=0; I<4; ++I) for (int32 J=0; J<4; ++J) for (int32 K=0; K<4; ++K) R.M[I][J]+=M[I][K]*B.M[K][J];
    return R;
}
FTransform FMatrix44::ToUnreal() const
{
    // H R H in UE row-vector storage: transpose the mathematical rotation exactly once.
    const double H[3]={1,-1,1};
    FMatrix R=FMatrix::Identity;
    for (int32 I=0; I<3; ++I) for (int32 J=0; J<3; ++J) R.M[J][I]=H[I]*M[I][J]*H[J];
    FQuat Q(R); Q.Normalize();
    return FTransform(Q, FVector(100*M[0][3], -100*M[1][3], 100*M[2][3]));
}
bool ReadJson(const FString& Text, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    if (Text.Len()>65536) return Fail(Error,TEXT("JSON size budget"));
    int32 Depth=0; bool Quoted=false, Escaped=false;
    for (TCHAR Ch : Text)
    {
        if (Quoted) { if (Escaped) Escaped=false; else if (Ch==TEXT('\\')) Escaped=true; else if (Ch==TEXT('"')) Quoted=false; }
        else if (Ch==TEXT('"')) Quoted=true;
        else if (Ch==TEXT('{') || Ch==TEXT('[')) { if (++Depth>16) return Fail(Error,TEXT("JSON depth budget")); }
        else if (Ch==TEXT('}') || Ch==TEXT(']')) --Depth;
    }
    // UE's DOM parser overwrites duplicate fields. Detect them in the token stream first.
    struct FScope { bool Object;TSet<FString> Keys; };
    TArray<FScope> Scopes;auto Reader=TJsonReaderFactory<>::Create(Text);EJsonNotation Token;
    while (Reader->ReadNext(Token))
    {
        if (Token==EJsonNotation::ObjectEnd || Token==EJsonNotation::ArrayEnd) {if (Scopes.Num()) Scopes.Pop();continue;}
        if (Scopes.Num() && Scopes.Last().Object)
        {
            const FString Key=Reader->GetIdentifier();
            if (Scopes.Last().Keys.Contains(Key)) return Fail(Error,TEXT("Duplicate JSON field: ")+Key);
            Scopes.Last().Keys.Add(Key);
        }
        if (Token==EJsonNotation::ObjectStart || Token==EJsonNotation::ArrayStart) Scopes.Add({Token==EJsonNotation::ObjectStart,{}});
    }
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) || !Out.IsValid()) return Fail(Error,TEXT("Invalid JSON object"));
    for (const auto& Pair:Out->Values) if (!FiniteJson(Pair.Value)) return Fail(Error,TEXT("Non-finite JSON number"));
    return true;
}
bool LoadJson(const FString& File, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text,*File)) return Fail(Error,TEXT("Cannot read ")+File);
    // Local golden vectors may exceed the network frame limit.
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)) return Fail(Error,TEXT("Invalid local JSON ")+File);
    return true;
}
FString JsonString(const TSharedRef<FJsonObject>& Object)
{
    FString Result; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Result)); return Result;
}
static bool HashFile(const FString& File, FString& Hash)
{
    TArray<uint8> Bytes; uint8 Digest[SHA256_DIGEST_LENGTH];
    if (!FFileHelper::LoadFileToArray(Bytes,*File) || SHA256(Bytes.GetData(),Bytes.Num(),Digest)==nullptr) return false;
    Hash=BytesToHex(Digest,SHA256_DIGEST_LENGTH).ToLower(); return true;
}
bool FProfile::Load(const FString& Directory, FString& Error)
{
    *this=FProfile();
    TSharedPtr<FJsonObject> P,C,B;
    const FString PF=Directory/TEXT("virtual_humanoid_44_v1.json"), CF=Directory/TEXT("virtual_zero_pi_v1.json");
    if (!LoadJson(PF,P,Error) || !LoadJson(CF,C,Error) || !LoadJson(Directory/TEXT("body35_capabilities.json"),B,Error)) return false;
    if (!HashFile(PF,Hash) || !HashFile(CF,CalibrationHash)) return Fail(Error,TEXT("SHA256 unavailable"));
    if (!P->TryGetStringField(TEXT("profile_id"),Id) || !C->TryGetStringField(TEXT("calibration_id"),CalibrationId) || !B->TryGetStringField(TEXT("capability_id"),CapabilityId)) return Fail(Error,TEXT("Profile identity"));
    const TArray<TSharedPtr<FJsonValue>> *Order=nullptr,*AxisArray=nullptr,*NodeArray=nullptr,*CalArray=nullptr;
    if (!P->TryGetArrayField(TEXT("axis_order"),Order) || !P->TryGetArrayField(TEXT("axes"),AxisArray) || !P->TryGetArrayField(TEXT("nodes"),NodeArray) || !C->TryGetArrayField(TEXT("axes"),CalArray)) return Fail(Error,TEXT("Profile arrays"));
    if (Order->Num()!=44 || AxisArray->Num()!=44 || CalArray->Num()!=44) return Fail(Error,TEXT("Expected 44 channels"));
    for (const auto& V : *Order)
    {
        FString Name; if (!V->TryGetString(Name) || AxisIndices.Contains(Name)) return Fail(Error,TEXT("Axis order not unique"));
        AxisIndices.Add(Name,Axes.Num()); FAxis A; A.Id=Name; Axes.Add(A);
    }
    TSet<FString> SeenAxes,SeenCal;
    for (const auto& V : *AxisArray)
    {
        if (V->Type!=EJson::Object) return Fail(Error,TEXT("Axis object"));
        const auto O=V->AsObject(); FString Name; TArray<double> Limits;
        if (!O->TryGetStringField(TEXT("id"),Name) || !AxisIndices.Contains(Name) || SeenAxes.Contains(Name) || !Numbers(*O,TEXT("limits_rad"),2,Limits) || Limits[0]>=Limits[1]) return Fail(Error,TEXT("Axis limits/identity"));
        SeenAxes.Add(Name); FAxis& A=Axes[AxisIndices[Name]]; A.Min=Limits[0]; A.Max=Limits[1];
    }
    for (const auto& V : *CalArray)
    {
        if (V->Type!=EJson::Object) return Fail(Error,TEXT("Calibration object"));
        const auto O=V->AsObject(); FString Name;
        if (!O->TryGetStringField(TEXT("axis_id"),Name) || !AxisIndices.Contains(Name) || SeenCal.Contains(Name)) return Fail(Error,TEXT("Calibration identity"));
        SeenCal.Add(Name); FAxis& A=Axes[AxisIndices[Name]];
        if (!Number(O->TryGetField(TEXT("zero_raw_rad")),A.Zero) || !Number(O->TryGetField(TEXT("sign")),A.Sign) || !Number(O->TryGetField(TEXT("joint_rad_per_sensor_rad")),A.Scale) || !Number(O->TryGetField(TEXT("raw_period_rad")),A.Period) || FMath::Abs(A.Sign)!=1 || A.Scale<=0 || A.Period<=0) return Fail(Error,TEXT("Invalid calibration"));
    }
    TSet<int32> Bound;
    for (const auto& V : *NodeArray)
    {
        if (V->Type!=EJson::Object) return Fail(Error,TEXT("Node object"));
        const auto O=V->AsObject(); FNode N; FString Parent,Kind;
        if (!O->TryGetStringField(TEXT("id"),N.Id) || NodeIndices.Contains(N.Id) || !O->TryGetStringField(TEXT("kind"),Kind)) return Fail(Error,TEXT("Node identity"));
        const auto ParentValue=O->TryGetField(TEXT("parent"));
        if (!ParentValue.IsValid()) return Fail(Error,TEXT("Missing parent"));
        if (ParentValue->Type!=EJson::Null)
        {
            if (!ParentValue->TryGetString(Parent) || !NodeIndices.Contains(Parent)) return Fail(Error,TEXT("Parent missing/cycle/unordered tree"));
            N.Parent=NodeIndices[Parent];
        }
        if (!Rigid(O->TryGetField(TEXT("parent_to_axis")),N.Before) || !Rigid(O->TryGetField(TEXT("axis_to_child")),N.After)) return Fail(Error,TEXT("Invalid rigid transform"));
        if (Kind==TEXT("revolute"))
        {
            FString Axis; TArray<double> Vector;
            if (!O->TryGetStringField(TEXT("axis_id"),Axis) || !AxisIndices.Contains(Axis) || !Numbers(*O,TEXT("axis_local"),3,Vector)) return Fail(Error,TEXT("Invalid axis"));
            N.AxisIndex=AxisIndices[Axis]; N.Axis=FVector3d(Vector[0],Vector[1],Vector[2]);
            if (FMath::Abs(N.Axis.SquaredLength()-1)>1e-8 || Bound.Contains(N.AxisIndex)) return Fail(Error,TEXT("Invalid axis norm/duplicate"));
            Bound.Add(N.AxisIndex);
        }
        else if (Kind!=TEXT("fixed")) return Fail(Error,TEXT("Unknown joint kind"));
        NodeIndices.Add(N.Id,Nodes.Num()); Nodes.Add(N);
    }
    if (Bound.Num()!=44) return Fail(Error,TEXT("Unbound axes"));
    const TSharedPtr<FJsonObject>* Semantic=nullptr;
    if (!P->TryGetObjectField(TEXT("anatomical_segments"),Semantic)) return Fail(Error,TEXT("Missing semantic segments"));
    for (const auto& Item : (*Semantic)->Values)
    {
        FString Node; if (!Item.Value->TryGetString(Node) || !NodeIndices.Contains(Node)) return Fail(Error,TEXT("Invalid semantic node"));
        Segments.Add(FString(*Item.Key),NodeIndices[Node]);
    }
    const TSharedPtr<FJsonObject>* Fixed=nullptr;
    if (!B->TryGetObjectField(TEXT("fixed_axis_values_rad"),Fixed)) return Fail(Error,TEXT("Missing fixed capability"));
    for (const auto& Item : (*Fixed)->Values)
    {
        const FString Key(*Item.Key);
        double Angle; if (!AxisIndices.Contains(Key) || !Number(Item.Value,Angle)) return Fail(Error,TEXT("Invalid fixed capability"));
        FixedAxes.Add(AxisIndices[Key],Angle);
    }
    return true;
}
bool FProfile::Forward(const TArray<double>& Angles, TArray<FMatrix44>& Out, FString& Error) const
{
    if (Angles.Num()!=Axes.Num()) return Fail(Error,TEXT("FK channel count"));
    for (double Q : Angles) if (!FMath::IsFinite(Q)) return Fail(Error,TEXT("FK non-finite angle"));
    Out.SetNum(Nodes.Num());
    for (int32 I=0; I<Nodes.Num(); ++I)
    {
        const FNode& N=Nodes[I];
        const FMatrix44 R=N.AxisIndex==INDEX_NONE ? FMatrix44::Identity() : FMatrix44::Rotation(N.Axis,Angles[N.AxisIndex]);
        Out[I]=(N.Parent==INDEX_NONE ? FMatrix44::Identity() : Out[N.Parent])*N.Before*R*N.After;
    }
    return true;
}
static bool Identity(const FProfile& P,const FJsonObject& O,FString& Error)
{
    const TPair<const TCHAR*,FString> Fields[]={{TEXT("protocol"),TEXT("posedoll.sensor/1")},{TEXT("profile_id"),P.Id},{TEXT("profile_sha256"),P.Hash},{TEXT("calibration_id"),P.CalibrationId},{TEXT("calibration_sha256"),P.CalibrationHash}};
    for (const auto& Pair:Fields) { FString V; if (!O.TryGetStringField(Pair.Key,V) || V!=Pair.Value) return Fail(Error,FString(TEXT("IdentityMismatch: "))+Pair.Key); }
    return true;
}
bool Handshake(const FProfile& P,const FJsonObject& O,FIdentity& Out,FString& Error)
{
    FString Type; double Hz=0;
    if (O.Values.Num()!=12 || !O.TryGetStringField(TEXT("type"),Type) || Type!=TEXT("hello") || !Identity(P,O,Error)) return Fail(Error,TEXT("Hello identity/fields mismatch: ")+Error);
    if (!O.TryGetStringField(TEXT("device_id"),Out.Device) || !O.TryGetStringField(TEXT("session_id"),Out.Session) || Out.Device.IsEmpty() || Out.Session.IsEmpty() || Out.Device.Len()>128 || Out.Session.Len()>128) return Fail(Error,TEXT("Invalid device/session"));
    if (!O.TryGetStringField(TEXT("capability_id"),Out.Capability) || (Out.Capability!=TEXT("full44") && Out.Capability!=P.CapabilityId)) return Fail(Error,TEXT("CapabilitiesUnsupported"));
    const TArray<TSharedPtr<FJsonValue>>* Order=nullptr;
    if (!O.TryGetArrayField(TEXT("axis_order"),Order) || Order->Num()!=P.Axes.Num()) return Fail(Error,TEXT("AxisOrderMismatch"));
    for (int32 I=0; I<Order->Num(); ++I) { FString Name; if (!(*Order)[I]->TryGetString(Name) || Name!=P.Axes[I].Id) return Fail(Error,TEXT("AxisOrderMismatch")); }
    FString Source;
    if (!O.TryGetStringField(TEXT("source_kind"),Source) || Source.IsEmpty() || !O.TryGetNumberField(TEXT("nominal_sample_hz"),Hz) || !FMath::IsFinite(Hz) || Hz<1 || Hz>240) return Fail(Error,TEXT("Invalid source/rate"));
    return true;
}
static bool UInt64(const FJsonObject& O,const TCHAR* Field,uint64& Out)
{
    FString S; if (!O.TryGetStringField(Field,S) || S.IsEmpty() || S.Len()>20 || (S.Len()>1 && S[0]==TEXT('0'))) return false;
    Out=0;
    for (TCHAR C:S) { if (C<TEXT('0') || C>TEXT('9')) return false; const uint64 D=C-TEXT('0'); if (Out>(MAX_uint64-D)/10) return false; Out=Out*10+D; }
    return true;
}
bool ParseSample(const FProfile& P,const FIdentity& ID,const FJsonObject& O,FSample& Out,FString& Error)
{
    FString Type,Device,Session;
    if (O.Values.Num()!=12 || !Identity(P,O,Error) || !O.TryGetStringField(TEXT("type"),Type) || Type!=TEXT("sample") || !O.TryGetStringField(TEXT("device_id"),Device) || Device!=ID.Device || !O.TryGetStringField(TEXT("session_id"),Session) || Session!=ID.Session) return Fail(Error,TEXT("Sample identity/fields mismatch: ")+Error);
    if (!UInt64(O,TEXT("sequence"),Out.Sequence) || !UInt64(O,TEXT("sender_monotonic_us"),Out.SenderMicros)) return Fail(Error,TEXT("Invalid uint64"));
    const TArray<TSharedPtr<FJsonValue>> *Raw=nullptr,*Status=nullptr;
    if (!O.TryGetArrayField(TEXT("raw_angles_rad"),Raw) || !O.TryGetArrayField(TEXT("axis_status"),Status) || Raw->Num()!=44 || Status->Num()!=44) return Fail(Error,TEXT("ChannelCount"));
    Out.Raw.SetNum(44); Out.Status.SetNum(44);
    for (int32 I=0; I<44; ++I)
    {
        FString State; if (!(*Status)[I]->TryGetString(State)) return Fail(Error,TEXT("Invalid axis status"));
        Out.Status[I]=State; Out.Raw[I]=0;
        const bool Fixed=ID.Capability==P.CapabilityId && P.FixedAxes.Contains(I);
        if (State==TEXT("valid"))
        {
            if (Fixed || !Number((*Raw)[I],Out.Raw[I]) || Out.Raw[I]<0 || Out.Raw[I]>=P.Axes[I].Period) return Fail(Error,TEXT("Raw range/status ")+P.Axes[I].Id);
        }
        else if (State==TEXT("fixed")) { if (!Fixed || (*Raw)[I]->Type!=EJson::Null) return Fail(Error,TEXT("Undeclared fixed axis")); }
        else if (State==TEXT("missing") || State==TEXT("invalid")) { if ((*Raw)[I]->Type!=EJson::Null) return Fail(Error,TEXT("Invalid null/status pair")); }
        else return Fail(Error,TEXT("Unknown status"));
    }
    return true;
}
void FDecoder::Reset() { bInitialized=false; PreviousRaw.Reset(); Unwrapped.Reset(); Filtered.Reset(); Sequence=Micros=0; }
bool FDecoder::Decode(const FProfile& P,const FIdentity& ID,const FSample& S,TArray<double>& Out,FString& Error)
{
    if (S.Raw.Num()!=P.Axes.Num() || S.Status.Num()!=P.Axes.Num()) return Fail(Error,TEXT("Decode channel count"));
    if (bInitialized && (S.Sequence<=Sequence || S.SenderMicros<=Micros)) return Fail(Error,TEXT("Non-monotonic sample"));
    const double Dt=bInitialized ? double(S.SenderMicros-Micros)*1e-6 : 0;
    TArray<double> Next,Angles; Next.SetNum(P.Axes.Num()); Angles.SetNum(P.Axes.Num());
    for (int32 I=0; I<P.Axes.Num(); ++I)
    {
        const FAxis& A=P.Axes[I];
        if (S.Status[I]==TEXT("fixed") && ID.Capability==P.CapabilityId && P.FixedAxes.Contains(I)) { Next[I]=0; Angles[I]=P.FixedAxes[I]; continue; }
        if (S.Status[I]!=TEXT("valid") || !FMath::IsFinite(S.Raw[I])) return Fail(Error,TEXT("Missing/invalid axis: ")+A.Id);
        const double Gain=A.Sign*A.Scale;
        if (!bInitialized)
        {
            const double Base=S.Raw[I]-A.Zero;
            const double L=FMath::Min(A.Min/Gain,A.Max/Gain), H=FMath::Max(A.Min/Gain,A.Max/Gain);
            const double First=FMath::CeilToDouble((L-Base-1e-9)/A.Period),Last=FMath::FloorToDouble((H-Base+1e-9)/A.Period);
            if (First!=Last) return Fail(Error,TEXT("Calibration branch ambiguous/out of limits: ")+A.Id);
            Next[I]=Base+First*A.Period;
        }
        else
        {
            const double D=S.Raw[I]-PreviousRaw[I];
            const double Delta=D-A.Period*FMath::FloorToDouble((D+A.Period/2)/A.Period);
            if (FMath::Abs(Delta*Gain)>MaximumSpeed*Dt+1e-6) return Fail(Error,TEXT("Angular speed: ")+A.Id);
            Next[I]=Unwrapped[I]+Delta;
        }
        const double Q=Next[I]*Gain;
        if (Q<A.Min-1e-8 || Q>A.Max+1e-8) return Fail(Error,TEXT("Joint limit: ")+A.Id);
        const double Alpha=!bInitialized || FilterSeconds<=0 ? 1 : 1-FMath::Exp(-Dt/FilterSeconds);
        Angles[I]=bInitialized ? Filtered[I]+Alpha*(Q-Filtered[I]) : Q;
    }
    PreviousRaw=S.Raw; Unwrapped=MoveTemp(Next); Filtered=Angles; Out=MoveTemp(Angles);
    Sequence=S.Sequence; Micros=S.SenderMicros; bInitialized=true; return true;
}
}

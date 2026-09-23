#include "PoseDollStatic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollStaticCrossTest,"PoseDoll.Static.CrossLanguageFaultMatrix",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPoseDollStaticCrossTest::RunTest(const FString&)
{
    using namespace PoseDoll;FProfile P;FString E;TSharedPtr<FJsonObject> Data;
    if(!P.Load(FPaths::ProjectDir()/TEXT("Shared/Profiles"),E)||!LoadJson(FPaths::ProjectDir()/TEXT("Shared/Fixtures/O4/static_cases.json"),Data,E)){AddError(E);return false;}
    for(const auto& V:Data->GetArrayField(TEXT("cases")))
    {
        const auto C=V->AsObject();const FString Name=C->GetStringField(TEXT("name"));FStaticIdentity ID;TSharedPtr<FJsonObject> Hello;
        if(!UnwrapStatic(*C->GetObjectField(TEXT("hello")),Hello,E)||!StaticHello(P,*Hello,ID,E)){AddError(Name+E);continue;}
        FStaticWindow W;W.Begin(C->GetStringField(TEXT("capture_id")),C->GetNumberField(TEXT("wall_start")));bool Fault=false;
        for(const auto& Row:C->GetArrayField(TEXT("messages")))
        {
            const auto R=Row->AsObject();TSharedPtr<FJsonObject> Payload;FStaticMessage M;
            if(!UnwrapStatic(*R->GetObjectField(TEXT("wire")),Payload,E)||!ParseStatic(P,ID,*Payload,M,E)){Fault=true;break;}
            M.Sample.ReceivedSeconds=R->GetNumberField(TEXT("received"));
            if(!W.Push(P,M,E)){Fault=true;break;}
            if(W.State==TEXT("SnapshotReady"))break;
        }
        const FString Expected=C->GetStringField(TEXT("expected"));
        TestEqual(Name+TEXT(" fault"),Fault,Expected==TEXT("fault"));
        if(!Fault)TestEqual(Name+TEXT(" accepted"),W.State==TEXT("SnapshotReady"),Expected==TEXT("ready"));
        if(Expected==TEXT("ready")){TestEqual(TEXT("Whole final scan"),W.Final.ScanId,uint64(7));TestEqual(TEXT("Common observation"),W.StableMicros,uint64(580000));}
    }
    auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("protocol"),TEXT("PDS1/1"));Payload->SetStringField(TEXT("type"),TEXT("test"));
    const auto Wire=StaticEnvelope(Payload);Wire->SetStringField(TEXT("crc32"),TEXT("00000000"));TSharedPtr<FJsonObject> Rejected;
    TestFalse(TEXT("CRC corruption rejected"),UnwrapStatic(*Wire,Rejected,E));
    // Calibrated absolute branch is solved anew between widely separated poses.
    FStaticMessage M;M.Sample.Raw.Init(0,44);M.Sample.Status.Init(TEXT("valid"),44);
    for(int32 I=0;I<44;++I)M.Sample.Raw[I]=P.Axes[I].Zero;
    for(int32 I=0;I<3;++I)M.Sample.Status[I]=TEXT("fixed");
    P.Axes[3].Min=FMath::DegreesToRadians(-175.);P.Axes[3].Max=FMath::DegreesToRadians(175.);TArray<double> Q;
    for(double Degrees:{170.,-170.})
    {
        const auto& A=P.Axes[3];double R=A.Zero+FMath::DegreesToRadians(Degrees)/(A.Sign*A.Scale);R-=A.Period*FMath::FloorToDouble(R/A.Period);M.Sample.Raw[3]=R;
        TestTrue(TEXT("Absolute discontinuous pose"),DecodeAbsolute(P,M,Q,E));if(Q.Num()==44)TestTrue(TEXT("No previous-pose turn guess"),FMath::Abs(FMath::RadiansToDegrees(Q[3])-Degrees)<1e-6);
    }
    P.Axes[3].Min=-7;P.Axes[3].Max=7;TestFalse(TEXT("Unknown multi-turn branch"),DecodeAbsolute(P,M,Q,E));
    return !HasAnyErrors();
}
#endif

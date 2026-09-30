#include "PoseDollStatic.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollStaticRouterTest,"PoseDoll.Static.RetiredRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPoseDollStaticRouterTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace PoseDoll;
    FStaticRouter R;
    TestTrue(TEXT("First request"),R.Begin(TEXT("old"),1));
    R.Retire(TEXT("old"),2);
    TestTrue(TEXT("Immediate next request"),R.Begin(TEXT("new"),2));
    TestTrue(TEXT("Late ack/scan ignored"),R.Route(TEXT("old"),2.1)==EStaticRoute::Retired);
    TestTrue(TEXT("Active traffic delivered"),R.Route(TEXT("new"),2.1)==EStaticRoute::Active);
    TestTrue(TEXT("Unknown is an error"),R.Route(TEXT("other"),2.1)==EStaticRoute::Unexpected);
    TestFalse(TEXT("No retired ID reuse"),R.Begin(TEXT("old"),2.1));
    R.Retire(TEXT("old"),20);
    TestTrue(TEXT("Repeated cancel does not refresh age"),R.Route(TEXT("old"),32)==EStaticRoute::Unexpected);
    R.Reset();
    for(int32 I=0;I<40;++I){const FString Id=FString::FromInt(I);R.Begin(Id,40);R.Retire(Id,40);}
    TestEqual(TEXT("Bounded retired IDs"),R.RetiredCount(),32);
    TestTrue(TEXT("Evicted IDs remain strict"),R.Route(TEXT("0"),41)==EStaticRoute::Unexpected);
    R.Reset();TestTrue(TEXT("Reconnect clears old session"),R.Route(TEXT("39"),41)==EStaticRoute::Unexpected);
    return !HasAnyErrors();
}
#endif

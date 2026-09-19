#include "PoseDollCore.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollGoldenTest,"PoseDoll.Core.GoldenVectors",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPoseDollGoldenTest::RunTest(const FString&)
{
    using namespace PoseDoll;
    const FString Shared=FPaths::ProjectDir()/TEXT("Shared");
    FProfile P; FString Error;
    if (!TestTrue(TEXT("Profile load: ")+Error,P.Load(Shared/TEXT("Profiles"),Error))) { AddError(Error); return false; }
    TestEqual(TEXT("44 channels"),P.Axes.Num(),44);
    TestEqual(TEXT("54 nodes"),P.Nodes.Num(),54);
    TestEqual(TEXT("20 semantic segments"),P.Segments.Num(),20);
    TSharedPtr<FJsonObject> Golden,Hello;
    if (!LoadJson(Shared/TEXT("Fixtures/golden_vectors.json"),Golden,Error) || !LoadJson(Shared/TEXT("Fixtures/hello.json"),Hello,Error)) { AddError(Error); return false; }
    FIdentity ID;
    if (!Handshake(P,*Hello,ID,Error)) { AddError(Error); return false; }
    double MaxError=0,MaxBridgeError=0;
    for (const auto& CaseValue:Golden->GetArrayField(TEXT("cases")))
    {
        const auto Case=CaseValue->AsObject();
        TSharedPtr<FJsonObject> Raw;
        if (!LoadJson(Shared/TEXT("Fixtures")/Case->GetStringField(TEXT("fixture")),Raw,Error)) { AddError(Error); return false; }
        FSample Sample; FDecoder Decoder; TArray<double> Angles; TArray<FMatrix44> Pose;
        if (!ParseSample(P,ID,*Raw,Sample,Error) || !Decoder.Decode(P,ID,Sample,Angles,Error) || !P.Forward(Angles,Pose,Error)) { AddError(Error); return false; }
        const auto Expected=Case->GetObjectField(TEXT("source_base_matrices"));
        const auto ExpectedUE=Case->GetObjectField(TEXT("ue_axis_cm_matrices"));
        for (int32 I=0; I<P.Nodes.Num(); ++I)
        {
            const auto Rows=Expected->GetArrayField(P.Nodes[I].Id);
            const auto UERows=ExpectedUE->GetArrayField(P.Nodes[I].Id);
            for (int32 R=0; R<4; ++R) for (int32 C=0; C<4; ++C) MaxError=FMath::Max(MaxError,FMath::Abs(Pose[I].M[R][C]-Rows[R]->AsArray()[C]->AsNumber()));
            const FTransform UE=Pose[I].ToUnreal();
            for (int32 C=0; C<4; ++C)
            {
                const FVector V=C==3 ? UE.GetTranslation() : UE.TransformVectorNoScale(C==0?FVector::XAxisVector:(C==1?FVector::YAxisVector:FVector::ZAxisVector));
                for (int32 R=0; R<3; ++R) MaxBridgeError=FMath::Max(MaxBridgeError,FMath::Abs(V[R]-UERows[R]->AsArray()[C]->AsNumber()));
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Independent C++ 5 fixtures x 54 nodes: canonical max %.12g; Unreal bridge max %.12g"),MaxError,MaxBridgeError));
    TestTrue(TEXT("canonical coefficient max < 1e-8"),MaxError<1e-8);
    TestTrue(TEXT("Unreal centimetre / basis max < 1e-6"),MaxBridgeError<1e-6);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollRejectTest,"PoseDoll.Core.RejectionAndOffsets",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPoseDollRejectTest::RunTest(const FString&)
{
    using namespace PoseDoll;
    FString Error; FProfile P;
    if (!P.Load(FPaths::ProjectDir()/TEXT("Shared/Profiles"),Error)) { AddError(Error); return false; }
    TArray<double> Q; Q.Init(0,44); TArray<FMatrix44> Pose;
    Q.SetNum(43); TestFalse(TEXT("Reject incomplete pose"),P.Forward(Q,Pose,Error));
    Q.Init(0,44); Q[0]=std::numeric_limits<double>::quiet_NaN(); TestFalse(TEXT("Reject NaN"),P.Forward(Q,Pose,Error));
    Q.Init(0,44); Q[P.AxisIndices[TEXT("elbow_l.flex")]]=UE_DOUBLE_PI/2;
    P.Nodes[P.NodeIndices[TEXT("elbow_l")]].After.M[0][3]=0.01;
    TestTrue(TEXT("Nonzero post-axis offset"),P.Forward(Q,Pose,Error));
    TestTrue(TEXT("Elbow rotated offset preserved"),FMath::Abs(Pose[P.NodeIndices[TEXT("hand_l")]].M[2][3]-.260)<1e-9);
    const auto A=FMatrix44::Rotation(FVector3d(1,0,0),.7),B=FMatrix44::Rotation(FVector3d(0,1,0),.4);
    TestTrue(TEXT("Serial rotation order matters"),FMath::Abs((A*B).M[1][0]-(B*A).M[1][0])>.1);
    TSharedPtr<FJsonObject> O;
    TestFalse(TEXT("Reject NaN JSON"),ReadJson(TEXT("{\"x\":NaN}"),O,Error));
    TestFalse(TEXT("Reject non-object JSON"),ReadJson(TEXT("[]"),O,Error));
    TestFalse(TEXT("Reject duplicate fields"),ReadJson(TEXT("{\"x\":1,\"x\":2}"),O,Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollDecoderTest,"PoseDoll.Core.ContinuousDecoder",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPoseDollDecoderTest::RunTest(const FString&)
{
    using namespace PoseDoll;FString Error;FProfile P;const FString Root=FPaths::ProjectDir()/TEXT("Shared");
    if (!P.Load(Root/TEXT("Profiles"),Error)) {AddError(Error);return false;}
    TSharedPtr<FJsonObject> Hello,Raw;LoadJson(Root/TEXT("Fixtures/hello.json"),Hello,Error);LoadJson(Root/TEXT("Fixtures/neutral.sample.json"),Raw,Error);
    FIdentity ID;FSample S;Handshake(P,*Hello,ID,Error);ParseSample(P,ID,*Raw,S,Error);
    P.Axes[0].Zero=.1;P.Axes[0].Min=-1;P.Axes[0].Max=1;S.Raw[0]=2*UE_DOUBLE_PI-.02;S.Sequence=1;S.SenderMicros=1000000;
    FDecoder D;TArray<double> Q;
    TestTrue(TEXT("Initial wrapped branch"),D.Decode(P,ID,S,Q,Error));TestTrue(TEXT("Initial angle -0.12"),FMath::Abs(Q[0]+.12)<1e-9);
    S.Sequence++;S.SenderMicros+=16667;S.Raw[0]=.02;
    TestTrue(TEXT("Cross zero continuously"),D.Decode(P,ID,S,Q,Error));TestTrue(TEXT("Crossed angle -0.08"),FMath::Abs(Q[0]+.08)<1e-9);
    TestFalse(TEXT("Duplicate sequence rejected"),D.Decode(P,ID,S,Q,Error));
    S.Sequence++;S.SenderMicros+=16667;S.Status[1]=TEXT("missing");
    TestFalse(TEXT("Missing axis rejects whole sample"),D.Decode(P,ID,S,Q,Error));
    S.Status[1]=TEXT("valid");TestTrue(TEXT("Failed sample did not commit decoder"),D.Decode(P,ID,S,Q,Error));
    D.Reset();P.Axes[0].Min=-7;P.Axes[0].Max=7;
    TestFalse(TEXT("Ambiguous startup requires calibration"),D.Decode(P,ID,S,Q,Error));
    return !HasAnyErrors();
}
#endif

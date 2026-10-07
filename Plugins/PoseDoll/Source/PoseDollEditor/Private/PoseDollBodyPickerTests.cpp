#include "SPoseDollBodyPicker.h"
#include "PoseDollSession.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollBodyHitTest, "PoseDoll.Editor.BodyPicker.HitTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPoseDollBodyHitTest::RunTest(const FString&)
{
    const auto& Regions = PoseDoll::CaptureBodyMap::Regions();
    TSet<FString> PartIds;
    for (const auto& Region : Regions) PartIds.Add(Region.Part);
    TestEqual(TEXT("No duplicate clickable regions"), PartIds.Num(), Regions.Num());
    TestEqual(TEXT("Every capture part is represented"), PartIds.Num(), PoseDoll::FSession::PartOptions().Num());
    for (const auto& Part : PoseDoll::FSession::PartOptions())
        TestTrue(*Part.Value, PartIds.Contains(Part.Key));

    // Every shaft and endpoint must pick the same semantic part at narrow, normal and wide sizes.
    // This catches shoulder/upper-arm and ankle/forefoot overlap, as well as mirrored left/right.
    for (const FVector2D Size : {FVector2D(220,420),FVector2D(340,260),FVector2D(340,420),FVector2D(680,520)})
    {
        for (int32 Index = 0; Index < Regions.Num(); ++Index)
        {
            const auto& Region = Regions[Index];
            TArray<FVector2D> Samples = Region.Points;
            for (int32 Point = 1; Point < Region.Points.Num(); ++Point)
                Samples.Add((Region.Points[Point-1]+Region.Points[Point])*.5);
            for (const auto& Sample : Samples)
                TestEqual(*(Region.Part+TEXT(" shaft / joint")),
                    PoseDoll::CaptureBodyMap::HitTest(PoseDoll::CaptureBodyMap::Project(Sample,Size),Size),Index);
            if (Region.Part.EndsWith(TEXT("_l"))) TestTrue(TEXT("Character left is on observer right"),Region.Points[0].X>180);
            if (Region.Part.EndsWith(TEXT("_r"))) TestTrue(TEXT("Character right is on observer left"),Region.Points[0].X<180);
        }
        TestEqual(TEXT("Background does not select a part"),PoseDoll::CaptureBodyMap::HitTest({4,4},Size),INDEX_NONE);
        TestEqual(TEXT("Outside canvas does not select a part"),PoseDoll::CaptureBodyMap::HitTest({-1,200},Size),INDEX_NONE);
        const FVector2D UpperArm = PoseDoll::CaptureBodyMap::Project({246,156},Size);
        TestEqual(TEXT("Fine lines have a generous hit target"),PoseDoll::CaptureBodyMap::HitTest(UpperArm+FVector2D(8,0),Size),5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseDollBodySelectionTest, "PoseDoll.Editor.BodyPicker.SelectionWithoutBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPoseDollBodySelectionTest::RunTest(const FString&)
{
    PoseDoll::FSession Session;
    Session.SetCustomParts({});
    Session.TogglePart(TEXT("clavicle_l"),true);
    Session.TogglePart(TEXT("upperarm_l"),true);
    TestEqual(TEXT("Selections accumulate before a rig is available"),Session.GetSelectedParts().Num(),2);
    Session.TogglePart(TEXT("clavicle_l"),false);
    const TSet<FString> Selected = Session.GetSelectedParts();
    TestEqual(TEXT("Deselect only the clicked part"),Selected.Num(),1);
    TestTrue(TEXT("Upper arm remains selected"),Selected.Contains(TEXT("upperarm_l")));
    TestFalse(TEXT("Selecting shoulder does not select descendants"),Selected.Contains(TEXT("lowerarm_l")));
    TestFalse(TEXT("Unknown parts are rejected"),Session.SetCustomParts({TEXT("not_a_part")}));
    TestTrue(TEXT("Rejected selection preserves existing choices"),Session.GetSelectedParts().Contains(TEXT("upperarm_l")));
    Session.SetCustomParts({});
    TestEqual(TEXT("Empty selection remains empty"),Session.GetSelectedParts().Num(),0);
    return true;
}
#endif

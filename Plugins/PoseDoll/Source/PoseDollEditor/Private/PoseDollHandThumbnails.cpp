#include "PoseDollHandPresets.h"
#include "PoseDollCore.h"
#include "PreviewScene.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"

namespace PoseDoll
{
bool FHandPresetLibrary::RenderThumbnails(UControlRig* Reference,USkeletalMesh* Mesh,FString& Error,int32 Start,int32 Count)
{
    if(!Reference||!Mesh||!Load(Error)){Error=TEXT("A validated mannequin rig is required for thumbnails");return false;}
    if(Start<0||Start>=Presets.Num()||Count<1||Count>8){Error=TEXT("Render a batch of 1 to 8 presets");return false;}
    TSharedPtr<FJsonObject> Settings;FString SettingsError;
    LoadJson(Root()/TEXT("capture.json"),Settings,SettingsError);
    auto Setting=[&](const TCHAR* Key,double Default){double Value=Default;if(Settings)Settings->TryGetNumberField(Key,Value);return Value;};
    FPreviewScene Scene{FPreviewScene::ConstructionValues()};
    Scene.SetLightBrightness(3.f);Scene.SetSkyBrightness(1.5f);
    auto* Target=NewObject<UTextureRenderTarget2D>();Target->ClearColor=FLinearColor(.025f,.032f,.042f,1.f);
    Target->InitCustomFormat(384,384,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);
    auto* Camera=NewObject<USceneCaptureComponent2D>();Camera->TextureTarget=Target;
    Camera->CaptureSource=SCS_FinalColorLDR;Camera->ProjectionType=ECameraProjectionMode::Orthographic;Camera->OrthoWidth=Setting(TEXT("width"),23.);
    Camera->bCaptureEveryFrame=false;Camera->bCaptureOnMovement=false;
    Camera->ShowFlags.SetEyeAdaptation(false);Camera->ShowFlags.SetMotionBlur(false);Camera->ShowFlags.SetTemporalAA(false);
    Camera->ShowFlags.SetGlobalIllumination(false);Camera->ShowFlags.SetAmbientOcclusion(false);
    Camera->PostProcessSettings.bOverride_AutoExposureMethod=true;Camera->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;Camera->PostProcessSettings.AutoExposureBias=0.f;
    Scene.AddComponent(Camera,FTransform::Identity);
    const FString Directory=Root()/TEXT("Thumbnails");IFileManager::Get().MakeDirectory(*Directory,true);
    const auto Neutral=ReadBones(Reference);
    for(int32 Index=Start;Index<FMath::Min(Start+Count,Presets.Num());++Index)
    {
        const auto& Preset=*Presets[Index];
        for(EHandSide Side:{EHandSide::Left,EHandSide::Right})
        {
            FEditPose Edit;if(!BuildEdit(Reference,Preset,Side,Edit,Error))return false;
            // Each capture gets a fresh skinning proxy. Several captures can occur
            // within one editor frame, where GPU bone revision numbers are otherwise reused.
            auto* Preview=NewObject<UPoseableMeshComponent>();Preview->SetSkinnedAssetAndUpdate(Mesh);
            Preview->SetCastShadow(false);Scene.AddComponent(Preview,FTransform::Identity);
            const FString S=Side==EHandSide::Left?TEXT("l"):TEXT("r");
            auto Point=[&](const FString& N){return Neutral.FindChecked(FName(*(N+S))).GetLocation();};
            const FVector OriginalWrist=Point(TEXT("hand_"));
            const FVector Up=(Point(TEXT("middle_03_"))-OriginalWrist).GetSafeNormal();
            const FVector Across=(Point(TEXT("pinky_01_"))-Point(TEXT("index_01_"))).GetSafeNormal();
            // Translate the whole arm out of the torso silhouette. This preserves
            // the wrist skin weights as well as every hand-local bone transform.
            const FVector Offset=Up*500.;
            for(auto& Bone:Edit.Bones)
            {
                const FString Name=Bone.Key.ToString();
                const bool Arm=Name.EndsWith(TEXT("_")+S)&&(Name.StartsWith(TEXT("clavicle_"))||Name.StartsWith(TEXT("upperarm_"))||Name.StartsWith(TEXT("lowerarm_"))||Name.StartsWith(TEXT("hand_")));
                if(Arm||IsHandBone(Bone.Key,Side))Bone.Value.AddToTranslation(Offset);
            }
            const FVector Wrist=OriginalWrist+Offset;
            const auto& Ref=Mesh->GetRefSkeleton();
            for(int32 BoneIndex=0;BoneIndex<Ref.GetNum();++BoneIndex)
            {
                const int32 Parent=Ref.GetParentIndex(BoneIndex);const FName Bone=Ref.GetBoneName(BoneIndex);
                if(const FTransform* Global=Edit.Bones.Find(Bone))
                {
                    const FTransform* ParentGlobal=Parent>=0?Edit.Bones.Find(Ref.GetBoneName(Parent)):nullptr;
                    Preview->BoneSpaceTransforms[BoneIndex]=ParentGlobal?Global->GetRelativeTransform(*ParentGlobal):*Global;
                }
            }
            Preview->MarkRefreshTransformDirty();Preview->RefreshBoneTransforms();Preview->UpdateBounds();Preview->MarkRenderTransformDirty();Preview->MarkRenderDynamicDataDirty();
            const FVector Normal=FVector::CrossProduct(Across,Up).GetSafeNormal()*(Side==EHandSide::Left?1.0:-1.0);
            const FVector Center=Wrist+Up*Setting(TEXT("center"),8.5);
            const FVector Eye=Center+Normal*Setting(TEXT("normal"),-45.)+Across*Setting(TEXT("across"),-20.)+Up*Setting(TEXT("up"),-10.);
            const FRotator Rotation=FRotationMatrix::MakeFromXZ((Center-Eye).GetSafeNormal(),Up).Rotator();
            Camera->SetWorldLocationAndRotation(Eye,Rotation);
            Scene.SetLightDirection((Center-(Eye+Up*30.0-Across*15.0)).Rotation());
            Scene.GetWorld()->SendAllEndOfFrameUpdates();FlushRenderingCommands();
            // Capture the exact fully evaluated UE skeletal pose; no external image is used.
            Camera->CaptureScene();FlushRenderingCommands();
            FImage Image;
            if(!FImageUtils::GetRenderTargetImage(Target,Image)||!FImageUtils::SaveImageByExtension(*Preset.ImagePath(Side),Image))
            {Error=TEXT("Could not save Unreal hand thumbnail: ")+Preset.Id;return false;}
            Scene.RemoveComponent(Preview);
            auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("renderer"),TEXT("Unreal Engine SceneCapture2D"));
            Record->SetStringField(TEXT("engine"),FEngineVersion::Current().ToString());Record->SetStringField(TEXT("mesh"),Mesh->GetPathName());
            Record->SetStringField(TEXT("rig_class"),Reference->GetClass()->GetPathName());Record->SetStringField(TEXT("preset"),Preset.Id);
            Record->SetStringField(TEXT("states"),Preset.States);Record->SetStringField(TEXT("side"),S);
            const auto& Controls=Side==EHandSide::Left?Preset.Left:Preset.Right;FString Signature;
            for(FName Name:ControlNames(Side))Signature+=Name.ToString()+Controls[Name].ToString()+TEXT("\n");
            Record->SetStringField(TEXT("controls_sha256"),Sha256Text(Signature));
            FFileHelper::SaveStringToFile(JsonString(Record),*(Directory/(Preset.Id+TEXT("_")+S+TEXT(".json"))));
        }
    }
    return true;
}
}

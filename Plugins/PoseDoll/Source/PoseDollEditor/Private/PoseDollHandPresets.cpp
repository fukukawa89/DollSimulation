#include "PoseDollHandPresets.h"
#include "PoseDollCore.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace PoseDoll
{
int32 FHandPreset::Count(TCHAR State) const {int32 N=0;for(TCHAR C:States)if(C==State)++N;return N;}
FString FHandPreset::ImagePath(EHandSide Side) const
{return FHandPresetLibrary::Root()/TEXT("Thumbnails")/(Id+(Side==EHandSide::Left?TEXT("_l.png"):TEXT("_r.png")));}
FHandPresetLibrary& FHandPresetLibrary::Get(){static FHandPresetLibrary Library;return Library;}
FString FHandPresetLibrary::Root(){return IPluginManager::Get().FindPlugin(TEXT("PoseDoll"))->GetBaseDir()/TEXT("Resources/HandPresets");}
const TArray<FString>& FHandPresetLibrary::Fingers()
{static const TArray<FString> Names={TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")};return Names;}
TArray<FName> FHandPresetLibrary::ControlNames(EHandSide Side)
{
    TArray<FName> Names;
    for(const FString S:{FString(TEXT("l")),FString(TEXT("r"))})
    {
        if((Side==EHandSide::Left && S!=TEXT("l"))||(Side==EHandSide::Right && S!=TEXT("r")))continue;
        for(const FString& Finger:Fingers())
        {
            if(Finger!=TEXT("thumb"))Names.Add(FName(*(Finger+TEXT("_metacarpal_")+S+TEXT("_ctrl"))));
            for(int32 Joint=1;Joint<=3;++Joint)Names.Add(FName(*FString::Printf(TEXT("%s_%02d_%s_ctrl"),*Finger,Joint,*S)));
        }
    }
    return Names;
}
bool FHandPresetLibrary::Load(FString& Error)
{
    if(!Presets.IsEmpty())return true;
    TSharedPtr<FJsonObject> RootObject;
    if(!LoadJson(Root()/TEXT("presets.json"),RootObject,Error))return false;
    int32 Version=0;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!RootObject->TryGetNumberField(TEXT("version"),Version)||Version!=1||!RootObject->TryGetArrayField(TEXT("presets"),Rows))
    {Error=TEXT("Invalid hand preset library schema");return false;}
    TArray<TSharedPtr<FHandPreset>> Candidate;TSet<FString> Ids;
    for(const auto& Row:*Rows)
    {
        const auto O=Row->AsObject();auto P=MakeShared<FHandPreset>();
        if(!O||!O->TryGetStringField(TEXT("id"),P->Id)||!O->TryGetStringField(TEXT("states"),P->States)||
           P->Id.IsEmpty()||FPaths::GetCleanFilename(P->Id)!=P->Id||P->Id.Contains(TEXT("."))||Ids.Contains(P->Id)||P->States.Len()!=5)
        {Error=TEXT("Invalid or duplicate hand preset identity");return false;}
        for(TCHAR C:P->States)if(C!='E'&&C!='B'&&C!='C'){Error=TEXT("Invalid hand state");return false;}
        for(EHandSide Side:{EHandSide::Left,EHandSide::Right})
        {
            const TSharedPtr<FJsonObject>* Values=nullptr;
            if(!O->TryGetObjectField(Side==EHandSide::Left?TEXT("left"):TEXT("right"),Values)){Error=TEXT("Missing hand control values");return false;}
            auto& Controls=Side==EHandSide::Left?P->Left:P->Right;
            const auto Names=ControlNames(Side);
            if((*Values)->Values.Num()!=Names.Num()){Error=TEXT("Hand presets must replace every finger and metacarpal animation control");return false;}
            for(FName Name:Names)
            {
                const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
                if(!(*Values)->TryGetArrayField(Name.ToString(),V)||V->Num()!=9){Error=TEXT("Invalid hand control transform: ")+Name.ToString();return false;}
                double A[9];for(int32 I=0;I<9;++I)if(!(*V)[I]->TryGetNumber(A[I])||!FMath::IsFinite(A[I])){Error=TEXT("Nonfinite hand preset transform");return false;}
                if(A[6]<=0||A[7]<=0||A[8]<=0){Error=TEXT("Invalid hand preset scale");return false;}
                Controls.Add(Name,FTransform(FRotator(A[4],A[5],A[3]),FVector(A[0],A[1],A[2]),FVector(A[6],A[7],A[8])));
            }
        }
        Ids.Add(P->Id);Candidate.Add(P);
    }
    if(Candidate.IsEmpty()){Error=TEXT("The hand preset library is empty");return false;}
    Presets=MoveTemp(Candidate);return true;
}
const FHandPreset* FHandPresetLibrary::Find(const FString& Id) const
{for(const auto& P:Presets)if(P->Id==Id)return P.Get();return nullptr;}
bool FHandPresetLibrary::IsHandBone(FName Name,EHandSide Side)
{
    const FString N=Name.ToString();
    const bool SideMatch=(Side!=EHandSide::Left&&N.EndsWith(TEXT("_r")))||(Side!=EHandSide::Right&&N.EndsWith(TEXT("_l")));
    if(!SideMatch)return false;
    for(const FString& F:Fingers())if(N.StartsWith(F+TEXT("_")))return true;
    return false;
}
TMap<FName,FTransform> FHandPresetLibrary::ReadBones(UControlRig* Rig)
{TMap<FName,FTransform> Out;for(const auto& K:Rig->GetHierarchy()->GetBoneKeys())Out.Add(K.Name,Rig->GetHierarchy()->GetGlobalTransform(K));return Out;}
bool FHandPresetLibrary::VerifyBones(UControlRig* Rig,const TMap<FName,FTransform>& Expected,FString& Error)
{
    for(const auto& P:Expected)
    {
        const FTransform Actual=Rig->GetHierarchy()->GetGlobalTransform(FRigElementKey(P.Key,ERigElementType::Bone));
        const double A=FMath::RadiansToDegrees(Actual.GetRotation().GetNormalized().AngularDistance(P.Value.GetRotation().GetNormalized()));
        const double D=FVector::Distance(Actual.GetLocation(),P.Value.GetLocation());
        if(A>.05||D>.01){Error=FString::Printf(TEXT("Hand pose bone verification failed: %s (%.4f deg, %.4f cm)"),*P.Key.ToString(),A,D);return false;}
    }
    return true;
}
bool FHandPresetLibrary::BuildEdit(UControlRig* Rig,const FHandPreset& Preset,EHandSide Side,FEditPose& Out,FString& Error)
{
    Out=FEditPose();if(!Rig){Error=TEXT("No Control Rig target");return false;}
    if(Side!=EHandSide::Right)Out.Controls.Append(Preset.Left);
    if(Side!=EHandSide::Left)Out.Controls.Append(Preset.Right);
    for(const auto& P:Out.Controls)
    {
        const auto* C=Rig->FindControl(P.Key);
        if(!C||C->Settings.ControlType!=ERigControlType::EulerTransform){Error=TEXT("Unsupported hand control: ")+P.Key.ToString();return false;}
    }
    TStrongObjectPtr<UControlRig> Work(NewObject<UControlRig>(GetTransientPackage(),Rig->GetClass(),NAME_None,RF_Transient));
    Work->SetObjectBinding(Rig->GetObjectBinding());Work->Initialize();Work->RequestConstruction();Work->Evaluate_AnyThread();
    Work->GetHierarchy()->CopyPose(Rig->GetHierarchy(),true,true,true);Work->Evaluate_AnyThread();
    const auto Before=ReadBones(Work.Get());
    // Curl widgets are non-animatable proxies. Replacing all 19 driven controls
    // per hand replaces their result without keying or accumulating proxy deltas.
    for(FName N:ControlNames(Side))Work->SetControlLocalTransform(N,Out.Controls[N],false,FRigControlModifiedContext(EControlRigSetKey::Never),false);
    Work->Evaluate_AnyThread();Out.Bones=ReadBones(Work.Get());
    TMap<FName,FTransform> Protected;
    for(const auto& P:Before)if(!IsHandBone(P.Key,Side))Protected.Add(P.Key,P.Value);
    return VerifyBones(Work.Get(),Protected,Error);
}
FString FHandPresetLibrary::Report() const
{
    auto O=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Items;
    for(const auto& P:Presets){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),P->Id);Row->SetStringField(TEXT("states"),P->States);Row->SetStringField(TEXT("left_image"),P->ImagePath(EHandSide::Left));Row->SetStringField(TEXT("right_image"),P->ImagePath(EHandSide::Right));Items.Add(MakeShared<FJsonValueObject>(Row));}
    O->SetArrayField(TEXT("presets"),Items);return JsonString(O);
}
}

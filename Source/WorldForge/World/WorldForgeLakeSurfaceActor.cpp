#include "WorldForgeLakeSurfaceActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    void CollectScalarParameterNames(const UMaterialInterface* Material, TSet<FName>& OutParameterNames)
    {
        if (!Material)
        {
            return;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);

        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            OutParameterNames.Add(ParameterInfo.Name);
        }
    }

    void CollectVectorParameterNames(const UMaterialInterface* Material, TSet<FName>& OutParameterNames)
    {
        if (!Material)
        {
            return;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        Material->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);

        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            OutParameterNames.Add(ParameterInfo.Name);
        }
    }

    bool HasParameter(const TSet<FName>& ParameterNames, const TCHAR* ParameterName)
    {
        return ParameterName && ParameterNames.Contains(FName(ParameterName));
    }

    FString GetMaterialParentName(const UMaterialInterface* Material)
    {
        if (!Material)
        {
            return TEXT("None");
        }

        if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Material))
        {
            if (const UMaterialInterface* Parent = MI->Parent)
            {
                return Parent->GetName();
            }
        }

        return Material->GetName();
    }

    // Applies inland lake visual tuning using the actual Waterline V2 parameter names
    // found by inspecting MI_Lake_V2_Near. Falls back to generic names for non-Waterline materials.
    // Returns true and assigns a DynMat if at least one parameter was applied.
    bool ApplySafeLakeMaterialTuning(
        UStaticMeshComponent* MeshComponent,
        UMaterialInterface* Material,
        FString& OutReason)
    {
        if (!MeshComponent || !Material)
        {
            OutReason = TEXT("NullInput");
            return false;
        }

        TSet<FName> ScalarParameterNames;
        TSet<FName> VectorParameterNames;
        CollectScalarParameterNames(Material, ScalarParameterNames);
        CollectVectorParameterNames(Material, VectorParameterNames);

        // Detect Waterline V2 lake parameters (actual names from MI_Lake_V2_Near inspection).
        // Note: Waterline uses intentional typos in some parameter names ("Streght", "Sruface").
        const bool bHasWaterlineParams =
            HasParameter(VectorParameterNames, TEXT("Deep Water Color")) ||
            HasParameter(VectorParameterNames, TEXT("Sruface Color Pit")) ||
            HasParameter(VectorParameterNames, TEXT("Shallow Water Color")) ||
            HasParameter(ScalarParameterNames, TEXT("Wave Normal Streght")) ||
            HasParameter(ScalarParameterNames, TEXT("Foam Texture Strenght"));

        // Generic parameter names checked as fallback for non-Waterline water materials.
        const bool bHasGenericParams =
            HasParameter(VectorParameterNames, TEXT("BaseColor")) ||
            HasParameter(VectorParameterNames, TEXT("WaterColor")) ||
            HasParameter(VectorParameterNames, TEXT("ShallowColor")) ||
            HasParameter(VectorParameterNames, TEXT("DeepColor")) ||
            HasParameter(ScalarParameterNames, TEXT("Opacity")) ||
            HasParameter(ScalarParameterNames, TEXT("Roughness")) ||
            HasParameter(ScalarParameterNames, TEXT("WaveIntensity")) ||
            HasParameter(ScalarParameterNames, TEXT("WaveStrength")) ||
            HasParameter(ScalarParameterNames, TEXT("FoamIntensity"));

        if (!bHasWaterlineParams && !bHasGenericParams)
        {
            OutReason = TEXT("NoTunableParameters");
            return false;
        }

        UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(Material, MeshComponent);
        if (!DynamicMaterial)
        {
            OutReason = TEXT("DynMatCreateFailed");
            return false;
        }

        bool bAppliedAnyParameter = false;

        // -----------------------------------------------------------------------
        // Waterline V2 inland lake color targets.
        // Replaces the bright ocean-blue defaults with dark teal-green tones
        // suited to freshwater inland lakes.
        // -----------------------------------------------------------------------
        const FLinearColor InlandDeepColor(0.010f, 0.048f, 0.042f, 1.0f);
        const FLinearColor InlandShallowColor(0.006f, 0.040f, 0.032f, 1.0f);
        const FLinearColor InlandSurfacePit(0.004f, 0.018f, 0.022f, 1.0f);
        const FLinearColor InlandSurfaceCrest(0.048f, 0.110f, 0.088f, 1.0f);
        const FLinearColor InlandSubSurface(0.030f, 0.140f, 0.090f, 1.0f);
        const FLinearColor InlandHorizonColor(0.020f, 0.048f, 0.052f, 0.0f);

        if (HasParameter(VectorParameterNames, TEXT("Deep Water Color")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Deep Water Color"), InlandDeepColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Deep Water Color RT")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Deep Water Color RT"), InlandDeepColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Shallow Water Color")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Shallow Water Color"), InlandShallowColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Sruface Color Pit")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Sruface Color Pit"), InlandSurfacePit);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Sruface Color Crest")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Sruface Color Crest"), InlandSurfaceCrest);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Sub-Surface Color")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Sub-Surface Color"), InlandSubSurface);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("Horizon Color")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("Horizon Color"), InlandHorizonColor);
            bAppliedAnyParameter = true;
        }

        // -----------------------------------------------------------------------
        // Calmer wave surface — inland lakes have much less wave energy than ocean.
        // -----------------------------------------------------------------------
        if (HasParameter(ScalarParameterNames, TEXT("Wave Normal Streght")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Wave Normal Streght"), 0.18f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Wave Normal Speed")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Wave Normal Speed"), 0.12f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Wave Speed")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Wave Speed"), 0.035f);
            bAppliedAnyParameter = true;
        }

        // -----------------------------------------------------------------------
        // Reduced foam — inland lakes produce minimal foam.
        // -----------------------------------------------------------------------
        if (HasParameter(ScalarParameterNames, TEXT("Foam Texture Strenght")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Foam Texture Strenght"), 8.0f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Dynamic Foam Foam Bubbles Strenght")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Dynamic Foam Foam Bubbles Strenght"), 0.4f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Dynamic Foam Power")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Dynamic Foam Power"), 0.08f);
            bAppliedAnyParameter = true;
        }

        // -----------------------------------------------------------------------
        // Higher roughness for a flatter, more diffuse surface (less ocean gloss).
        // -----------------------------------------------------------------------
        if (HasParameter(ScalarParameterNames, TEXT("Roughness Wave Crest")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Roughness Wave Crest"), 0.50f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Roughness Wave Pit")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Roughness Wave Pit"), 0.40f);
            bAppliedAnyParameter = true;
        }

        // -----------------------------------------------------------------------
        // Generic parameter fallbacks for non-Waterline water materials.
        // -----------------------------------------------------------------------
        const FLinearColor GenericBaseColor(0.070f, 0.115f, 0.125f, 1.0f);
        const FLinearColor GenericShallowColor(0.110f, 0.165f, 0.155f, 1.0f);
        const FLinearColor GenericDeepColor(0.040f, 0.080f, 0.095f, 1.0f);

        if (HasParameter(VectorParameterNames, TEXT("BaseColor")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("BaseColor"), GenericBaseColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("WaterColor")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("WaterColor"), GenericBaseColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("ShallowColor")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("ShallowColor"), GenericShallowColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(VectorParameterNames, TEXT("DeepColor")))
        {
            DynamicMaterial->SetVectorParameterValue(TEXT("DeepColor"), GenericDeepColor);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Opacity")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.92f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("Roughness")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.55f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("WaveIntensity")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("WaveIntensity"), 0.15f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("WaveStrength")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("WaveStrength"), 0.15f);
            bAppliedAnyParameter = true;
        }

        if (HasParameter(ScalarParameterNames, TEXT("FoamIntensity")))
        {
            DynamicMaterial->SetScalarParameterValue(TEXT("FoamIntensity"), 0.05f);
            bAppliedAnyParameter = true;
        }

        if (bAppliedAnyParameter)
        {
            MeshComponent->SetMaterial(0, DynamicMaterial);
            OutReason = bHasWaterlineParams ? TEXT("WaterlineV2ParamsApplied") : TEXT("GenericParamsApplied");
        }
        else
        {
            OutReason = TEXT("NoParametersMatched");
        }

        return bAppliedAnyParameter;
    }
}

AWorldForgeLakeSurfaceActor::AWorldForgeLakeSurfaceActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    LakeSurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LakeSurfaceMesh"));
    LakeSurfaceMesh->SetupAttachment(SceneRoot);
    LakeSurfaceMesh->SetMobility(EComponentMobility::Movable);
    LakeSurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LakeSurfaceMesh->SetGenerateOverlapEvents(false);
    LakeSurfaceMesh->SetCastShadow(false);
}

void AWorldForgeLakeSurfaceActor::InitializeLakeSurface(
    const FWaterlineLakeSurfaceDesc& Desc,
    UStaticMesh* Mesh,
    UMaterialInterface* Material,
    float BaseHalfExtent,
    float CoverageScale,
    float MinHalfExtent,
    float MinScaleXY,
    float MaxScaleXY,
    float ZOffset)
{
    const float SafeBaseHalfExtent = FMath::Max(1.0f, BaseHalfExtent);
    const float SafeCoverageScale = FMath::Clamp(CoverageScale, 0.01f, 1.0f);
    const float SafeMinHalfExtent = FMath::Max(0.0f, MinHalfExtent);
    const float SafeMinScaleXY = FMath::Max(0.01f, MinScaleXY);
    const float SafeMaxScaleXY = FMath::Max(SafeMinScaleXY, MaxScaleXY);

    const float BasinHalfExtentX = Desc.HalfExtentX;
    const float BasinHalfExtentY = Desc.HalfExtentY;
    const float EffectiveBasinHalfExtentX = FMath::Max(BasinHalfExtentX, SafeMinHalfExtent);
    const float EffectiveBasinHalfExtentY = FMath::Max(BasinHalfExtentY, SafeMinHalfExtent);
    const float DesiredHalfExtentX = EffectiveBasinHalfExtentX * SafeCoverageScale;
    const float DesiredHalfExtentY = EffectiveBasinHalfExtentY * SafeCoverageScale;

    float MeshBaseHalfExtentX = SafeBaseHalfExtent;
    float MeshBaseHalfExtentY = SafeBaseHalfExtent;
    if (Mesh)
    {
        const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
        if (MeshBounds.BoxExtent.X > KINDA_SMALL_NUMBER)
        {
            MeshBaseHalfExtentX = MeshBounds.BoxExtent.X;
        }

        if (MeshBounds.BoxExtent.Y > KINDA_SMALL_NUMBER)
        {
            MeshBaseHalfExtentY = MeshBounds.BoxExtent.Y;
        }
    }

    const float RawScaleX = DesiredHalfExtentX / MeshBaseHalfExtentX;
    const float RawScaleY = DesiredHalfExtentY / MeshBaseHalfExtentY;
    const float FinalScaleX = FMath::Clamp(RawScaleX, SafeMinScaleXY, SafeMaxScaleXY);
    const float FinalScaleY = FMath::Clamp(RawScaleY, SafeMinScaleXY, SafeMaxScaleXY);
    const bool bScaleClamped =
        !FMath::IsNearlyEqual(RawScaleX, FinalScaleX, 0.01f) ||
        !FMath::IsNearlyEqual(RawScaleY, FinalScaleY, 0.01f);

    const float RawZ = Desc.WaterSurfaceZ;
    const float FinalZ = RawZ + ZOffset;

    const FVector FinalLocation(
        Desc.WorldCenter.X,
        Desc.WorldCenter.Y,
        FinalZ);
    const FVector FinalScale(
        FinalScaleX,
        FinalScaleY,
        1.0f);

    const bool bRenderableSurface = (Mesh != nullptr) && (Material != nullptr);

    SetActorEnableCollision(false);
    SetActorHiddenInGame(!bRenderableSurface);
    SetActorTransform(FTransform(FRotator::ZeroRotator, FinalLocation, FinalScale));

    LakeSurfaceMesh->SetStaticMesh(Mesh);
    LakeSurfaceMesh->SetVisibility(bRenderableSurface, true);
    LakeSurfaceMesh->SetHiddenInGame(!bRenderableSurface);

    if (Material)
    {
        FString TuneReason;
        const bool bAppliedMaterialTuning = ApplySafeLakeMaterialTuning(LakeSurfaceMesh, Material, TuneReason);
        if (!bAppliedMaterialTuning)
        {
            LakeSurfaceMesh->SetMaterial(0, Material);
        }

        const FString ParentName = GetMaterialParentName(Material);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[WaterlineLakeLight] LakeMaterial Path=%s Parent=%s Tuned=%s Reason=%s"),
            *GetPathNameSafe(Material),
            *ParentName,
            bAppliedMaterialTuning ? TEXT("true") : TEXT("false"),
            *TuneReason);

        if (!bAppliedMaterialTuning)
        {
            // TODO: Create /Game/WorldForge/Materials/MI_WF_InlandLake for distinct inland lake look.
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[WaterlineLakeLight] WARNING: Lightweight lake is using default Waterline material and may look like ocean water."));
        }
    }
    else
    {
        LakeSurfaceMesh->SetMaterial(0, nullptr);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeLight] Height Basin=%d RawZ=%.1f Offset=%.1f FinalZ=%.1f"),
        Desc.BasinID,
        RawZ,
        ZOffset,
        FinalZ);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeVisual] Basin=%d BasinHalfExtent=(%.0f,%.0f) DesiredWaterHalfExtent=(%.0f,%.0f) MeshHalfExtent=(%.1f,%.1f) RawScale=(%.2f,%.2f) ClampMax=%.2f FinalScale=(%.2f,%.2f)"),
        Desc.BasinID,
        BasinHalfExtentX,
        BasinHalfExtentY,
        DesiredHalfExtentX,
        DesiredHalfExtentY,
        MeshBaseHalfExtentX,
        MeshBaseHalfExtentY,
        RawScaleX,
        RawScaleY,
        SafeMaxScaleXY,
        FinalScaleX,
        FinalScaleY);

    if (bScaleClamped)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WaterlineLakeVisual] WARNING: Lake surface scale was clamped. Basin=%d RawScale=(%.2f,%.2f) FinalScale=(%.2f,%.2f)"),
            Desc.BasinID,
            RawScaleX,
            RawScaleY,
            FinalScaleX,
            FinalScaleY);
    }
}

#include "WorldForgeRiverSurfaceActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Applies subtle river-specific color tones using the same Waterline V2 parameter
    // names as the lake tuning, but shifted toward flowing-water greens.
    bool ApplySafeRiverMaterialTuning(
        UStaticMeshComponent* MeshComponent,
        UMaterialInterface* Material)
    {
        if (!MeshComponent || !Material)
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;

        TSet<FName> VectorParamNames;
        TSet<FName> ScalarParamNames;

        Material->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& P : ParameterInfos)
        {
            VectorParamNames.Add(P.Name);
        }

        ParameterInfos.Reset();
        ParameterIds.Reset();

        Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& P : ParameterInfos)
        {
            ScalarParamNames.Add(P.Name);
        }

        const bool bHasWaterlineParams =
            VectorParamNames.Contains(FName(TEXT("Deep Water Color"))) ||
            VectorParamNames.Contains(FName(TEXT("Shallow Water Color"))) ||
            VectorParamNames.Contains(FName(TEXT("Sruface Color Pit")));

        const bool bHasGenericParams =
            VectorParamNames.Contains(FName(TEXT("BaseColor"))) ||
            VectorParamNames.Contains(FName(TEXT("WaterColor")));

        if (!bHasWaterlineParams && !bHasGenericParams)
        {
            return false;
        }

        UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(Material, MeshComponent);
        if (!DynMat)
        {
            return false;
        }

        // River: slightly darker and greener than lakes — shallow, faster-moving water.
        const FLinearColor RiverDeep(0.008f, 0.038f, 0.032f, 1.0f);
        const FLinearColor RiverShallow(0.014f, 0.055f, 0.040f, 1.0f);
        const FLinearColor RiverSurfacePit(0.005f, 0.016f, 0.018f, 1.0f);

        bool bApplied = false;

        if (VectorParamNames.Contains(FName(TEXT("Deep Water Color"))))
        {
            DynMat->SetVectorParameterValue(TEXT("Deep Water Color"), RiverDeep);
            bApplied = true;
        }
        if (VectorParamNames.Contains(FName(TEXT("Shallow Water Color"))))
        {
            DynMat->SetVectorParameterValue(TEXT("Shallow Water Color"), RiverShallow);
            bApplied = true;
        }
        if (VectorParamNames.Contains(FName(TEXT("Sruface Color Pit"))))
        {
            DynMat->SetVectorParameterValue(TEXT("Sruface Color Pit"), RiverSurfacePit);
            bApplied = true;
        }
        if (VectorParamNames.Contains(FName(TEXT("BaseColor"))))
        {
            DynMat->SetVectorParameterValue(TEXT("BaseColor"), RiverDeep);
            bApplied = true;
        }
        if (VectorParamNames.Contains(FName(TEXT("WaterColor"))))
        {
            DynMat->SetVectorParameterValue(TEXT("WaterColor"), RiverDeep);
            bApplied = true;
        }

        // Calmer, slightly faster surface — rivers flow linearly.
        if (ScalarParamNames.Contains(FName(TEXT("Wave Normal Streght"))))
        {
            DynMat->SetScalarParameterValue(TEXT("Wave Normal Streght"), 0.08f);
            bApplied = true;
        }
        if (ScalarParamNames.Contains(FName(TEXT("Wave Normal Speed"))))
        {
            DynMat->SetScalarParameterValue(TEXT("Wave Normal Speed"), 0.40f);
            bApplied = true;
        }
        if (ScalarParamNames.Contains(FName(TEXT("Wave Speed"))))
        {
            DynMat->SetScalarParameterValue(TEXT("Wave Speed"), 0.18f);
            bApplied = true;
        }
        if (ScalarParamNames.Contains(FName(TEXT("Foam Texture Strenght"))))
        {
            DynMat->SetScalarParameterValue(TEXT("Foam Texture Strenght"), 4.0f);
            bApplied = true;
        }

        if (bApplied)
        {
            MeshComponent->SetMaterial(0, DynMat);
        }

        return bApplied;
    }
}

AWorldForgeRiverSurfaceActor::AWorldForgeRiverSurfaceActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    RiverSurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RiverSurfaceMesh"));
    RiverSurfaceMesh->SetupAttachment(SceneRoot);
    RiverSurfaceMesh->SetMobility(EComponentMobility::Movable);
    RiverSurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RiverSurfaceMesh->SetGenerateOverlapEvents(false);
    RiverSurfaceMesh->SetCastShadow(false);
}

void AWorldForgeRiverSurfaceActor::InitializeRiverSurface(
    const FWorldForgeRiverSurfaceDesc& Desc,
    UStaticMesh* Mesh,
    UMaterialInterface* Material,
    float TileSizeWorld,
    float ZOffset)
{
    const bool bRenderable = (Mesh != nullptr) && (Material != nullptr);

    SetActorEnableCollision(false);
    SetActorHiddenInGame(!bRenderable);

    // Compute tile-space bounding rectangle from the river path.
    int32 MinTX = Desc.StartTile.X;
    int32 MaxTX = Desc.StartTile.X;
    int32 MinTY = Desc.StartTile.Y;
    int32 MaxTY = Desc.StartTile.Y;
    for (const FIntPoint& P : Desc.TilePath)
    {
        MinTX = FMath::Min(MinTX, P.X);
        MaxTX = FMath::Max(MaxTX, P.X);
        MinTY = FMath::Min(MinTY, P.Y);
        MaxTY = FMath::Max(MaxTY, P.Y);
    }

    const float SpanX = static_cast<float>(MaxTX - MinTX + 1);
    const float SpanY = static_cast<float>(MaxTY - MinTY + 1);

    // World-space center: offset from WorldStart by half the bounding span.
    const float DeltaTileX = (static_cast<float>(MinTX + MaxTX) * 0.5f) - static_cast<float>(Desc.StartTile.X);
    const float DeltaTileY = (static_cast<float>(MinTY + MaxTY) * 0.5f) - static_cast<float>(Desc.StartTile.Y);
    const FVector WorldCenter = FVector(
        Desc.WorldStart.X + DeltaTileX * TileSizeWorld,
        Desc.WorldStart.Y + DeltaTileY * TileSizeWorld,
        Desc.AverageWaterZ + ZOffset);

    // Scale plane mesh to cover the river bounding box.
    float MeshHalfExtentX = 50.0f;
    float MeshHalfExtentY = 50.0f;
    if (Mesh)
    {
        const FBoxSphereBounds B = Mesh->GetBounds();
        if (B.BoxExtent.X > KINDA_SMALL_NUMBER) MeshHalfExtentX = B.BoxExtent.X;
        if (B.BoxExtent.Y > KINDA_SMALL_NUMBER) MeshHalfExtentY = B.BoxExtent.Y;
    }

    const float SafeTileSize = FMath::Max(1.0f, TileSizeWorld);
    const float DesiredHalfX = FMath::Max(MeshHalfExtentX, SpanX * SafeTileSize * 0.5f);
    const float DesiredHalfY = FMath::Max(MeshHalfExtentY, SpanY * SafeTileSize * 0.5f);
    const float ScaleX = FMath::Clamp(DesiredHalfX / MeshHalfExtentX, 0.01f, 500.0f);
    const float ScaleY = FMath::Clamp(DesiredHalfY / MeshHalfExtentY, 0.01f, 500.0f);

    SetActorTransform(FTransform(FRotator::ZeroRotator, WorldCenter, FVector(ScaleX, ScaleY, 1.0f)));

    RiverSurfaceMesh->SetStaticMesh(Mesh);
    RiverSurfaceMesh->SetVisibility(bRenderable, true);
    RiverSurfaceMesh->SetHiddenInGame(!bRenderable);

    if (Material)
    {
        const bool bTuned = ApplySafeRiverMaterialTuning(RiverSurfaceMesh, Material);
        if (!bTuned)
        {
            RiverSurfaceMesh->SetMaterial(0, Material);
        }
    }
    else
    {
        RiverSurfaceMesh->SetMaterial(0, nullptr);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WaterlineRiverLight] Spawn River=%d Actor=%s Tiles=%d Segments=%d Width=%.1f AvgZ=%.1f Flow=%.2f"),
        Desc.RiverID,
        *GetName(),
        Desc.TileCount,
        Desc.TilePath.Num(),
        Desc.AverageWidth,
        Desc.AverageWaterZ,
        Desc.FlowSpeed);
}

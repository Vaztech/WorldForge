// Source/WorldForge/Player/MiningGhostActor.cpp

#include "Player/MiningGhostActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMiningGhost, Log, All);

// =============================================================================
// Constructor
// =============================================================================

AMiningGhostActor::AMiningGhostActor()
{
    PrimaryActorTick.bCanEverTick = false;

    CubeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CubeMesh"));
    SetRootComponent(CubeMesh);

    CubeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CubeMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    CubeMesh->CastShadow = false;
    CubeMesh->SetReceivesDecals(false);

    // ── Native fallback: engine built-in 1m³ cube ────────────────────────────
    // The engine cube is centred at origin, 1x1x1 metre (100x100x100 UU at scale 1).
    // Assign via Blueprint Class Defaults to override if needed.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeFinder.Succeeded())
    {
        GhostCubeMesh = CubeFinder.Object;
        CubeMesh->SetStaticMesh(GhostCubeMesh);
    }

    // The actor starts hidden — BuildModeManager shows it when needed.
    SetActorHiddenInGame(true);
    bGhostVisible = false;
}

// =============================================================================
// BeginPlay
// =============================================================================

void AMiningGhostActor::BeginPlay()
{
    Super::BeginPlay();

    // Apply the static mesh (may have been overridden in BP defaults).
    if (GhostCubeMesh)
    {
        CubeMesh->SetStaticMesh(GhostCubeMesh);
    }

    // Scale cube to match voxel size.
    // The engine cube is 100 UU at scale 1.0, so scale = VoxelSizeUU / 100.
    // Subtract a tiny fraction (0.98) so there is a slim gap between adjacent
    // voxel cubes, making them visually distinct rather than merging.
    const float Scale = (VoxelSizeUU / 100.0f) * 0.97f;
    CubeMesh->SetRelativeScale3D(FVector(Scale));

    // Build the dynamic material instance. Prefer the explicit C++ variable, but
    // also accept a material assigned directly to the Blueprint's CubeMesh component.
    // This is important for data-only BP_MiningGhostActor setups where Element 0 is
    // assigned in the component details instead of the MiningGhostMaterial variable.
    UMaterialInterface* SourceMaterial = MiningGhostMaterial.Get();
    if (!SourceMaterial && CubeMesh)
    {
        SourceMaterial = CubeMesh->GetMaterial(0);
        if (SourceMaterial)
        {
            MiningGhostMaterial = SourceMaterial;
        }
    }

    if (SourceMaterial)
    {
        GhostMID = CubeMesh->CreateDynamicMaterialInstance(0, SourceMaterial);

        if (!GhostMID)
        {
            UE_LOG(LogMiningGhost, Warning,
                TEXT("[MiningGhost] Failed to create dynamic material instance from %s."),
                *SourceMaterial->GetName());
        }
    }
    else
    {
        UE_LOG(LogMiningGhost, Warning,
            TEXT("[MiningGhost] No mining ghost material found. Assign either MiningGhostMaterial or CubeMesh Element 0 in BP_MiningGhostActor."));
    }

    ApplyMaterialState();
}

UStaticMesh* AMiningGhostActor::GetResolvedGhostCubeMesh() const
{
    if (GhostCubeMesh)
    {
        return GhostCubeMesh.Get();
    }

    return CubeMesh ? CubeMesh->GetStaticMesh() : nullptr;
}

UMaterialInterface* AMiningGhostActor::GetResolvedGhostMaterial() const
{
    if (MiningGhostMaterial)
    {
        return MiningGhostMaterial.Get();
    }

    return CubeMesh ? CubeMesh->GetMaterial(0) : nullptr;
}

// =============================================================================
// Runtime API
// =============================================================================

void AMiningGhostActor::UpdateHoverPosition(const FIntVector& VoxelCoord)
{
    CurrentVoxel = VoxelCoord;
    SetActorLocation(VoxelToWorldCenter(VoxelCoord));

    if (!bGhostVisible)
    {
        ShowGhost();
    }
}

void AMiningGhostActor::HideGhost()
{
    if (!bGhostVisible) return;
    bGhostVisible = false;
    SetActorHiddenInGame(true);
}

void AMiningGhostActor::ShowGhost()
{
    if (bGhostVisible) return;
    bGhostVisible = true;
    SetActorHiddenInGame(false);
}

void AMiningGhostActor::SetConfirmedState(bool bNewConfirmed)
{
    if (bConfirmed == bNewConfirmed) return;
    bConfirmed = bNewConfirmed;
    ApplyMaterialState();
}

// =============================================================================
// Internal helpers
// =============================================================================

FVector AMiningGhostActor::VoxelToWorldCenter(const FIntVector& VoxelCoord) const
{
    // Voxel coordinates are integer grid positions.
    // World centre = (VoxelCoord * VoxelSizeUU) + half voxel offset.
    // The +0.5 on each axis places the cube centre inside the voxel.
    return FVector(
        (VoxelCoord.X + 0.5f) * VoxelSizeUU,
        (VoxelCoord.Y + 0.5f) * VoxelSizeUU,
        (VoxelCoord.Z + 0.5f) * VoxelSizeUU
    );
}

void AMiningGhostActor::ApplyMaterialState()
{
    if (!GhostMID) return;

    if (bConfirmed)
    {
        GhostMID->SetScalarParameterValue(TEXT("GhostOpacity"), ConfirmedOpacity);
        GhostMID->SetVectorParameterValue(TEXT("GhostTint"),    ConfirmedTint);
    }
    else
    {
        GhostMID->SetScalarParameterValue(TEXT("GhostOpacity"), HoverOpacity);
        GhostMID->SetVectorParameterValue(TEXT("GhostTint"),    HoverTint);
    }
}

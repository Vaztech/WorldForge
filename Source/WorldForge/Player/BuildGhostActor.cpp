// Source/WorldForge/Player/BuildGhostActor.cpp
#include "BuildGhostActor.h"
#include "BuildingTagHelper.h"
#include "VoxelWorldManager.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    static const FName PendingGhostSupportTag(TEXT("WorldForgePendingBlockGhost"));
    static const FName PendingGhostCursorTag(TEXT("WorldForgePendingCursorGhost"));

    static void ConfigureAsPendingGhostSupport(UStaticMeshComponent* Comp)
    {
        if (!Comp)
        {
            return;
        }

        Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
        Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Comp->SetGenerateOverlapEvents(false);

        Comp->ComponentTags.Remove(PendingGhostCursorTag);
        if (!Comp->ComponentHasTag(PendingGhostSupportTag))
        {
            Comp->ComponentTags.Add(PendingGhostSupportTag);
        }
    }

    static void ConfigureAsPendingGhostCursor(UStaticMeshComponent* Comp)
    {
        if (!Comp)
        {
            return;
        }

        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
        Comp->SetGenerateOverlapEvents(false);

        Comp->ComponentTags.Remove(PendingGhostSupportTag);
        if (!Comp->ComponentHasTag(PendingGhostCursorTag))
        {
            Comp->ComponentTags.Add(PendingGhostCursorTag);
        }
    }

    static void DisableGhostTrace(UStaticMeshComponent* Comp)
    {
        if (!Comp)
        {
            return;
        }

        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
        Comp->SetGenerateOverlapEvents(false);
        Comp->ComponentTags.Remove(PendingGhostSupportTag);
        Comp->ComponentTags.Remove(PendingGhostCursorTag);
    }
}

ABuildGhostActor::ABuildGhostActor()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMesh"));
    GhostMesh->SetupAttachment(Root);
    GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GhostMesh->SetCastShadow(false);
    GhostMesh->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeFinder.Succeeded())
    {
        GhostMesh->SetStaticMesh(CubeFinder.Object);
    }
}

void ABuildGhostActor::BeginPlay()
{
    Super::BeginPlay();

    UMaterialInterface* BaseMat = GhostMesh->GetMaterial(0);
    if (BaseMat)
    {
        GhostMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (GhostMaterial)
        {
            GhostMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
            GhostMesh->SetMaterial(0, GhostMaterial);
        }
    }

    if (BaseMat)
    {
        CellValidMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (CellValidMaterial)
        {
            CellValidMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity));
            CellValidMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
        }

        CellCursorMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (CellCursorMaterial)
        {
            CellCursorMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.2f, 0.5f, 1.0f, GhostOpacity));
            CellCursorMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
        }

        CellInvalidMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (CellInvalidMaterial)
        {
            CellInvalidMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.9f, 0.1f, 0.1f, GhostOpacity));
            CellInvalidMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
        }
    }
}

void ABuildGhostActor::SetGhostState(EBuildGhostState State)
{
    CurrentState = State;

    if (State == EBuildGhostState::Hidden)
    {
        GhostMesh->SetVisibility(false);
        for (int32 i = 0; i < ActiveHighlightCount; ++i)
        {
            if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
            {
                CellHighlightPool[i]->SetVisibility(false);
                DisableGhostTrace(CellHighlightPool[i]);
            }
        }
        return;
    }

    if (GhostMaterial)
    {
        GhostMaterial->SetVectorParameterValue(FName("BaseColor"), GetColorForState(State));
    }
}

FLinearColor ABuildGhostActor::GetColorForState(EBuildGhostState State) const
{
    switch (State)
    {
    case EBuildGhostState::Valid:
        return FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity);
    case EBuildGhostState::Invalid:
        return FLinearColor(0.9f, 0.1f, 0.1f, GhostOpacity);
    default:
        return FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
    }
}

// TryParseExplicitPrefabDimensions moved to UBuildingTagHelper::TryParsePrefabDimensions

FIntPoint ABuildGhostActor::RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const
{
    const int32 SafeRotation = ((RotationIndex % 4) + 4) % 4;

    switch (SafeRotation)
    {
    case 0:
        return LocalXY;
    case 1:
        return FIntPoint(Depth - 1 - LocalXY.Y, LocalXY.X);
    case 2:
        return FIntPoint(Width - 1 - LocalXY.X, Depth - 1 - LocalXY.Y);
    case 3:
        return FIntPoint(LocalXY.Y, Width - 1 - LocalXY.X);
    default:
        return LocalXY;
    }
}

// GetLegacyPrefabBlockCount moved to UBuildingTagHelper::GetBlockCountForBuildingTag

void ABuildGhostActor::UpdatePrefabGhost(
    FVector WorldPosition,
    int32 RotationIndex,
    FGameplayTag BuildingTag,
    UVoxelWorldManager* VoxelManager,
    float SurfaceZ)
{
    if (!VoxelManager)
    {
        return;
    }

    GhostMesh->SetVisibility(false);

    TArray<FIntVector> PreviewBlocks;

    int32 Width = 0;
    int32 Depth = 0;
    int32 Height = 0;

    if (UBuildingTagHelper::TryParsePrefabDimensions(BuildingTag.ToString(), Width, Depth, Height))
    {
        PreviewBlocks.Reserve(Width * Depth * Height);

        for (int32 Z = 0; Z < Height; ++Z)
        {
            for (int32 Y = 0; Y < Depth; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const FIntPoint RotatedLocal = RotateLocalCell(FIntPoint(X, Y), RotationIndex, Width, Depth);
                    PreviewBlocks.Add(FIntVector(RotatedLocal.X, RotatedLocal.Y, Z));
                }
            }
        }
    }
    else
    {
        const int32 LegacyCount = UBuildingTagHelper::GetBlockCountForBuildingTag(BuildingTag);
        PreviewBlocks.Reserve(LegacyCount);

        for (int32 BlockNumber = 0; BlockNumber < LegacyCount; ++BlockNumber)
        {
            const int32 Layer = BlockNumber / 4;
            const int32 BlockIndexWithinLayer = BlockNumber % 4;

            FIntPoint LocalOffset = FIntPoint::ZeroValue;
            switch (BlockIndexWithinLayer)
            {
            case 0: LocalOffset = FIntPoint(0, 0); break;
            case 1: LocalOffset = FIntPoint(1, 0); break;
            case 2: LocalOffset = FIntPoint(0, 1); break;
            case 3: LocalOffset = FIntPoint(1, 1); break;
            default: break;
            }

            const FIntPoint RotatedLocal = RotateLocalCell(LocalOffset, RotationIndex, 2, 2);
            PreviewBlocks.Add(FIntVector(RotatedLocal.X, RotatedLocal.Y, Layer));
        }
    }

    EnsurePoolSize(PreviewBlocks.Num());

    const FIntVector OriginVoxel = VoxelManager->WorldToGrid3D(WorldPosition);

    int32 Idx = 0;
    for (const FIntVector& LocalBlock : PreviewBlocks)
    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            const FVector Pos = VoxelManager->GridToWorld(
                FIntPoint(OriginVoxel.X + LocalBlock.X, OriginVoxel.Y + LocalBlock.Y),
                OriginVoxel.Z + LocalBlock.Z
            );

            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);
            Comp->SetWorldScale3D(FVector(GhostCubeInsetScale));
            DisableGhostTrace(Comp);

            if (CurrentState == EBuildGhostState::Invalid && CellInvalidMaterial)
            {
                Comp->SetMaterial(0, CellInvalidMaterial);
            }
            else if (CellValidMaterial)
            {
                Comp->SetMaterial(0, CellValidMaterial);
            }
        }
        ++Idx;
    }

    for (int32 i = Idx; i < ActiveHighlightCount; ++i)
    {
        if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
            DisableGhostTrace(CellHighlightPool[i]);
        }
    }

    ActiveHighlightCount = Idx;
}

void ABuildGhostActor::UpdateCustomDrawCells(
    const TArray<FIntPoint>& SelectedCells,
    FIntPoint CursorCell,
    UVoxelWorldManager* VoxelManager,
    float SurfaceZ)
{
    if (!VoxelManager) return;

    GhostMesh->SetVisibility(false);

    const int32 TotalNeeded = SelectedCells.Num() + 1;
    EnsurePoolSize(TotalNeeded);

    int32 Idx = 0;

    for (const FIntPoint& Cell : SelectedCells)
    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            FVector Pos = VoxelManager->GridToWorld(Cell, 0);
            Pos.Z = SurfaceZ;
            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CurrentState == EBuildGhostState::Invalid && CellInvalidMaterial)
            {
                Comp->SetMaterial(0, CellInvalidMaterial);
            }
            else if (CellValidMaterial)
            {
                Comp->SetMaterial(0, CellValidMaterial);
            }

            Comp->SetWorldScale3D(FVector(1.0f));
            ConfigureAsPendingGhostSupport(Comp);
        }
        ++Idx;
    }

    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            FVector Pos = VoxelManager->GridToWorld(CursorCell, 0);
            Pos.Z = SurfaceZ;
            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CurrentState == EBuildGhostState::Invalid && CellInvalidMaterial)
            {
                Comp->SetMaterial(0, CellInvalidMaterial);
            }
            else if (CellCursorMaterial)
            {
                Comp->SetMaterial(0, CellCursorMaterial);
            }

            Comp->SetWorldScale3D(FVector(1.0f));
            ConfigureAsPendingGhostCursor(Comp);
        }
        ++Idx;
    }

    for (int32 i = Idx; i < ActiveHighlightCount; ++i)
    {
        if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
            DisableGhostTrace(CellHighlightPool[i]);
        }
    }

    ActiveHighlightCount = Idx;
}

void ABuildGhostActor::UpdateCustomDrawCells3D(
    const TArray<FIntVector>& SelectedCells3D,
    FIntVector CursorVoxel,
    UVoxelWorldManager* VoxelManager)
{
    if (!VoxelManager) return;

    GhostMesh->SetVisibility(false);

    const int32 TotalNeeded = SelectedCells3D.Num() + 1;
    EnsurePoolSize(TotalNeeded);

    int32 Idx = 0;

    for (const FIntVector& Cell : SelectedCells3D)
    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            const FVector Pos = VoxelManager->GridToWorld(
                FIntPoint(Cell.X, Cell.Y),
                Cell.Z
            );

            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CurrentState == EBuildGhostState::Invalid && CellInvalidMaterial)
            {
                Comp->SetMaterial(0, CellInvalidMaterial);
            }
            else if (CellValidMaterial)
            {
                Comp->SetMaterial(0, CellValidMaterial);
            }

            Comp->SetWorldScale3D(FVector(GhostCubeInsetScale));
            ConfigureAsPendingGhostSupport(Comp);
        }
        ++Idx;
    }

    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            const FVector Pos = VoxelManager->GridToWorld(
                FIntPoint(CursorVoxel.X, CursorVoxel.Y),
                CursorVoxel.Z
            );

            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CurrentState == EBuildGhostState::Invalid && CellInvalidMaterial)
            {
                Comp->SetMaterial(0, CellInvalidMaterial);
            }
            else if (CellCursorMaterial)
            {
                Comp->SetMaterial(0, CellCursorMaterial);
            }

            Comp->SetWorldScale3D(FVector(GhostCubeInsetScale));
            ConfigureAsPendingGhostCursor(Comp);
        }
        ++Idx;
    }

    for (int32 i = Idx; i < ActiveHighlightCount; ++i)
    {
        if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
            DisableGhostTrace(CellHighlightPool[i]);
        }
    }

    ActiveHighlightCount = Idx;
}

void ABuildGhostActor::ClearAllHighlights()
{
    for (int32 i = 0; i < CellHighlightPool.Num(); ++i)
    {
        if (CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
            DisableGhostTrace(CellHighlightPool[i]);
        }
    }
    ActiveHighlightCount = 0;

    GhostMesh->SetVisibility(false);
}

UStaticMeshComponent* ABuildGhostActor::GetOrCreateHighlight(int32 Index)
{
    if (CellHighlightPool.IsValidIndex(Index) && CellHighlightPool[Index])
    {
        return CellHighlightPool[Index];
    }

    UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this);
    if (!NewComp) return nullptr;

    NewComp->SetupAttachment(GetRootComponent());
    NewComp->RegisterComponent();
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    NewComp->SetGenerateOverlapEvents(false);
    NewComp->SetCastShadow(false);
    NewComp->SetVisibility(false);

    if (GhostMesh && GhostMesh->GetStaticMesh())
    {
        NewComp->SetStaticMesh(GhostMesh->GetStaticMesh());
    }

    NewComp->SetWorldScale3D(FVector(1.0f));
    DisableGhostTrace(NewComp);

    if (Index < CellHighlightPool.Num())
    {
        CellHighlightPool[Index] = NewComp;
    }
    else
    {
        CellHighlightPool.Add(NewComp);
    }

    return NewComp;
}

void ABuildGhostActor::EnsurePoolSize(int32 RequiredSize)
{
    while (CellHighlightPool.Num() < RequiredSize)
    {
        CellHighlightPool.Add(nullptr);
    }
}
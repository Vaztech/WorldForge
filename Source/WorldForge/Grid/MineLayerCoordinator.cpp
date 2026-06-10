// Source/WorldForge/Grid/MineLayerCoordinator.cpp

#include "Grid/MineLayerCoordinator.h"
#include "Grid/LayerViewSubsystem.h"
#include "Grid/GridSelectionSubsystem.h"
#include "Visual/TerrainCutawaySubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMineLayerCoord, Log, All);

// =============================================================================
// Lifecycle
// =============================================================================

void UMineLayerCoordinator::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogMineLayerCoord, Log, TEXT("[MineLayerCoord] Initialized."));
}

// =============================================================================
// Mine mode lifecycle
// =============================================================================

void UMineLayerCoordinator::InitializeForMineMode(float SurfaceWorldZ)
{
    // Forward to TerrainCutawaySubsystem: start unclipped so surface terrain
    // remains fully visible on entry. The player scrolls down to reveal layers.
    if (UTerrainCutawaySubsystem* Cutaway = GetCutaway())
    {
        Cutaway->EnterCutawayAtWorldZ(SurfaceWorldZ, false);
    }

    UE_LOG(LogMineLayerCoord, Log,
        TEXT("[MineLayerCoord] Mine mode initialized. SurfaceWorldZ=%.1f"),
        SurfaceWorldZ);
}

// =============================================================================
// Layer movement
// =============================================================================

void UMineLayerCoordinator::MoveLayer(int32 Delta)
{
    if (Delta == 0)
    {
        return;
    }

    // CUBE-MINING-V3-ACCESS: Each layer-down input now moves exactly ONE voxel.
    // Planning is hard-capped to MaxDepthForLog (3) layers below the deepest
    // currently open/exposed column. Removing the ×3 slice scaling is what makes
    // the spec's "16 → 15 → 14 → 13, then stop" behavior work: the step is 1 and
    // the cap is 3, instead of conflating the two into a single 3-voxel jump.
    static constexpr int32 MaxDepthForLog = 3;

    const int32 SteppedDelta = Delta; // 1:1 — no slice scaling.

    // Capture pre-move Z for the SliceMove log.
    int32 SurfaceZ = 0;
    int32 PreMoveZ = 0;
    if (const ULayerViewSubsystem* LV = GetLayerView())
    {
        SurfaceZ = LV->GetSurfaceVoxelLayerZ();
        PreMoveZ = LV->HasActiveVoxelLayer() ? LV->GetActiveVoxelLayerZ() : SurfaceZ;
    }
    const int32 RequestedZ = PreMoveZ + SteppedDelta;
    const int32 MinZ = FMath::Max(0, SurfaceZ - MaxDepthForLog);

    // GridSelectionSubsystem::AdjustActiveToolLayer moves LayerViewSubsystem
    // internally and then triggers a grid rebuild. We call it first, then sync
    // the cutaway to the new LayerView state so all three systems agree.
    if (UGridSelectionSubsystem* Grid = GetGrid())
    {
        Grid->AdjustActiveToolLayer(SteppedDelta);
    }

    SyncCutawayToLayerView();

    const int32 FinalZ = GetActiveVoxelLayerZ();
    const int32 RelOffset = GetRelativeLayerOffset();

    UE_LOG(LogMineLayerCoord, Log,
        TEXT("[CUBE-MINING-V2-UG] SliceMove Delta=%d Step=%d SurfaceZ=%d RequestedZ=%d FinalZ=%d Relative=%d MinZ=%d MaxZ=%d"),
        Delta, SteppedDelta, SurfaceZ, RequestedZ, FinalZ, RelOffset, MinZ, SurfaceZ);

    UE_LOG(LogMineLayerCoord, Log,
        TEXT("[MineLayerCoord] MoveLayer Delta=%d → VoxelZ=%d RelativeOffset=%d Underground=%s"),
        Delta,
        FinalZ,
        RelOffset,
        IsUnderground() ? TEXT("true") : TEXT("false"));
}

void UMineLayerCoordinator::ResetToSurface()
{
    // GridSelectionSubsystem::ResetActiveToolLayer resets LayerView to surface
    // and rebuilds the grid at the surface layer.
    if (UGridSelectionSubsystem* Grid = GetGrid())
    {
        Grid->ResetActiveToolLayer();
    }

    // Sync cutaway: surface offset = 0 → clip disabled.
    SyncCutawayToLayerView();

    UE_LOG(LogMineLayerCoord, Log,
        TEXT("[MineLayerCoord] ResetToSurface → VoxelZ=%d"),
        GetActiveVoxelLayerZ());
}

// =============================================================================
// Queries
// =============================================================================

int32 UMineLayerCoordinator::GetActiveVoxelLayerZ() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerView())
    {
        return LayerView->GetActiveVoxelLayerZ();
    }
    return 0;
}

bool UMineLayerCoordinator::IsUnderground() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerView())
    {
        return LayerView->GetRelativeLayerOffset() < 0;
    }
    return false;
}

int32 UMineLayerCoordinator::GetRelativeLayerOffset() const
{
    if (const ULayerViewSubsystem* LayerView = GetLayerView())
    {
        return LayerView->GetRelativeLayerOffset();
    }
    return 0;
}

// =============================================================================
// Internal: cutaway sync
// =============================================================================

void UMineLayerCoordinator::SyncCutawayToLayerView() const
{
    const ULayerViewSubsystem* LayerView = GetLayerView();
    UTerrainCutawaySubsystem* Cutaway = GetCutaway();

    if (!LayerView || !Cutaway || !LayerView->HasActiveVoxelLayer())
    {
        return;
    }

    // Surface (offset 0)  → bShouldClip=false  → full terrain visible.
    // Underground (< 0)   → bShouldClip=true   → terrain clipped at LayerWorldZ.
    const bool  bShouldClip = LayerView->GetRelativeLayerOffset() < 0;
    const float LayerWorldZ = LayerView->GetActiveLayerWorldZ();

    Cutaway->SetAbsoluteClipLayerWorldZ(LayerWorldZ, bShouldClip);

    UE_LOG(LogMineLayerCoord, Verbose,
        TEXT("[MineLayerCoord] Cutaway synced. LayerWorldZ=%.1f Clip=%s"),
        LayerWorldZ,
        bShouldClip ? TEXT("true") : TEXT("false"));
}

// =============================================================================
// Internal: subsystem accessors
// =============================================================================

ULayerViewSubsystem* UMineLayerCoordinator::GetLayerView() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<ULayerViewSubsystem>() : nullptr;
}

UTerrainCutawaySubsystem* UMineLayerCoordinator::GetCutaway() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UTerrainCutawaySubsystem>() : nullptr;
}

UGridSelectionSubsystem* UMineLayerCoordinator::GetGrid() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UGridSelectionSubsystem>() : nullptr;
}

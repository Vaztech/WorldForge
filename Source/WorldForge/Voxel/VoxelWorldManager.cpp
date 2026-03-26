#include "VoxelWorldManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// MODIFIED:
// Use the real Voxel Plugin Legacy runtime headers for native voxel writes.
#include "VoxelWorld.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelIntBox.h"

void UVoxelWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Initialized."));
}

bool UVoxelWorldManager::IsReady()
{
    if (VoxelWorldActor)
    {
        return true;
    }

    return FindAndCacheVoxelWorld();
}

bool UVoxelWorldManager::FindAndCacheVoxelWorld()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] FindAndCacheVoxelWorld failed: GetWorld() returned null."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Searching for VoxelWorld..."));

    // Preferred version if/when you confirm the actual voxel actor class/header:
    //
    // #include "VoxelWorld.h"
    // for (TActorIterator<AVoxelWorld> It(World); It; ++It)
    // {
    //     VoxelWorldActor = *It;
    //     UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Found: %s"), *It->GetName());
    //     return true;
    // }

    // Fallback: class name contains "VoxelWorld"
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetClass()->GetName().Contains(TEXT("VoxelWorld")))
        {
            VoxelWorldActor = *It;
            UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Found: %s"), *It->GetName());
            return true;
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[VoxelWorldManager] No VoxelWorld actor found. Fix: Place a VoxelWorld actor in the current level.")
    );
    return false;
}

// ADDED:
FVector UVoxelWorldManager::GetVoxelWorldOrigin() const
{
    // ADDED:
    // Lazy attempt to resolve the voxel actor for conversion helpers too,
    // so click-to-grid can honor actor origin even before another system calls IsReady().
    if (!VoxelWorldActor)
    {
        const_cast<UVoxelWorldManager*>(this)->FindAndCacheVoxelWorld();
    }

    return VoxelWorldActor ? VoxelWorldActor->GetActorLocation() : FVector::ZeroVector;
}

FVector UVoxelWorldManager::GridToWorld(FIntPoint GridCell, int32 ZLevel) const
{
    // MODIFIED:
    // Apply voxel-world origin so conversion stays correct if the placed voxel actor is not at (0,0,0).
    const FVector Origin = GetVoxelWorldOrigin();

    const FVector Result(
        Origin.X + (GridCell.X * VoxelSize),
        Origin.Y + (GridCell.Y * VoxelSize),
        Origin.Z + (ZLevel * VoxelSize)
    );

    // ADDED:
    // Optional debug logging for conversion verification.
    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] GridToWorld Grid=(%d,%d,%d) Origin=(%.0f, %.0f, %.0f) -> World=(%.0f, %.0f, %.0f)"),
            GridCell.X,
            GridCell.Y,
            ZLevel,
            Origin.X,
            Origin.Y,
            Origin.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

// ADDED:
FIntVector UVoxelWorldManager::WorldToGrid3D(FVector WorldPosition) const
{
    // ADDED:
    // Defensive fallback to avoid division by zero if VoxelSize is misconfigured.
    const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;

    // ADDED:
    // Convert from world space into voxel-local space so clicks respect the voxel actor's transform.
    const FVector Origin = GetVoxelWorldOrigin();
    const FVector LocalPosition = WorldPosition - Origin;

    // ADDED:
    // Center-based snapping:
    // add half a voxel before floor so clicks resolve to the nearest voxel cell center
    // instead of always biasing downward to the lower cell edge.
    const int32 GridX = FMath::FloorToInt((LocalPosition.X + (SafeVoxelSize * 0.5f)) / SafeVoxelSize);
    const int32 GridY = FMath::FloorToInt((LocalPosition.Y + (SafeVoxelSize * 0.5f)) / SafeVoxelSize);
    const int32 GridZ = FMath::FloorToInt((LocalPosition.Z + (SafeVoxelSize * 0.5f)) / SafeVoxelSize);

    const FIntVector Result(GridX, GridY, GridZ);

    // ADDED:
    // Optional logging to confirm repeated clicks on the same tile map to the same grid coordinate.
    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] WorldToGrid3D World=(%.1f, %.1f, %.1f) Origin=(%.1f, %.1f, %.1f) Local=(%.1f, %.1f, %.1f) -> Grid=(%d,%d,%d)"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            Origin.X,
            Origin.Y,
            Origin.Z,
            LocalPosition.X,
            LocalPosition.Y,
            LocalPosition.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FIntPoint UVoxelWorldManager::WorldToGrid(FVector WorldPosition) const
{
    // MODIFIED:
    // Reuse the robust 3D conversion and keep the existing XY-only BP API unchanged.
    const FIntVector Grid3D = WorldToGrid3D(WorldPosition);
    return FIntPoint(Grid3D.X, Grid3D.Y);
}

void UVoxelWorldManager::ApplyBiomeProfileToChunk(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile)
{
    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk rejected: biome profile is not Smooth SDF.")
        );
        return;
    }

    if (!IsReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed: VoxelWorld still not available.")
        );
        return;
    }

    OnBiomeChunkRequested(ChunkCoord, Profile);
}

void UVoxelWorldManager::PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag)
{
    if (!IsReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] PlaceVoxelBlock failed: VoxelWorld still not available.")
        );
        return;
    }

    // ADDED:
    // Visible validation log for placement requests.
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] PlaceBlock (%.0f, %.0f, %.0f) Material=%s"),
        WorldPosition.X,
        WorldPosition.Y,
        WorldPosition.Z,
        MaterialTag.IsValid() ? *MaterialTag.ToString() : TEXT("None")
    );

    // MODIFIED:
    // Replace the fallback StaticMeshActor spawn with a real Voxel Plugin Legacy write.
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] PlaceVoxelBlock failed: cached actor is not AVoxelWorld.")
        );
        return;
    }

    // ADDED:
    // Convert world placement back into voxel-grid space so one construction block maps
    // to one voxel-sized box edit.
    const FIntVector GridCell = WorldToGrid3D(WorldPosition);

    // ADDED:
    // FVoxelIntBox is half-open [Min, Max), so +1 writes exactly one voxel cell.
    const FVoxelIntBox Bounds(
        FIntVector(GridCell.X, GridCell.Y, GridCell.Z),
        FIntVector(GridCell.X + 1, GridCell.Y + 1, GridCell.Z + 1)
    );

    // ADDED:
    // Native voxel-world mutation. Keep single-threaded for deterministic validation.
    UVoxelBoxTools::AddBox(
        VoxelWorld,
        Bounds,
        nullptr,   // OutModifiedValues
        nullptr,   // OutEditedBounds
        false,     // bMultiThreaded
        true       // bUpdateRender
    );

    // Existing Blueprint bridge remains unchanged.
    OnVoxelBlockPlaced(WorldPosition, MaterialTag);
}

void UVoxelWorldManager::NotifyVoxelDig(FVector WorldPosition, int32 AgentID)
{
    // AgentID intentionally unused for now; keep parameter for future gameplay/debug hooks.
    (void)AgentID;

    if (!IsReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] NotifyVoxelDig failed: VoxelWorld still not available.")
        );
        return;
    }

    OnVoxelDigged(WorldPosition);
}

bool UVoxelWorldManager::ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const
{
    return Profile.bSmoothSDF;
}
#include "VoxelWorldManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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

FVector UVoxelWorldManager::GridToWorld(FIntPoint GridCell, int32 ZLevel) const
{
    return FVector(
        GridCell.X * VoxelSize,
        GridCell.Y * VoxelSize,
        ZLevel * VoxelSize
    );
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
    // Visible validation log for V-H manual Blueprint testing.
    // Kept at Log level so it appears in normal Output Log filtering.
    // This does NOT change architecture or simulation behavior.
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] PlaceBlock (%.0f, %.0f, %.0f) Material=%s"),
        WorldPosition.X,
        WorldPosition.Y,
        WorldPosition.Z,
        MaterialTag.IsValid() ? *MaterialTag.ToString() : TEXT("None")
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
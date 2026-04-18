#include "VoxelWorldManager.h"

#include "VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "WorldForgeTerrainGenerator.h"
#include "VoxelWorldCreateInfo.h"

bool UVoxelWorldManager::GenerateTerrainFromTiles(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    ETerrainMode TerrainMode,
    int32 BlockHeightStep)
{
    ClearTerrainSnapshot();

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: invalid grid size %d x %d."),
            GridWidth,
            GridHeight
        );
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );
        return false;
    }

    CurrentTerrainMode = ETerrainMode::Smooth;
    CurrentBlockHeightStep = 1;

    if (TerrainMode == ETerrainMode::Blocky)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] Blocky terrain mode requested, but WorldForge is forcing Smooth mode to match GDD terrain direction.")
        );
    }

    ClearTerrainCache();

    PendingWorldTiles = WorldTiles;
    PendingGridWidth = GridWidth;
    PendingGridHeight = GridHeight;
    TerrainRetryCount = 0;

    if (!IsReady() || !IsVoxelRuntimeReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles deferred: VoxelWorld runtime is not ready yet. Mode=%s Step=%d"),
            CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
            GetEffectiveBlockHeightStep()
        );

        ScheduleTerrainRetry();
        return true;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: cached actor is not AVoxelWorld.")
        );

        ClearTerrainSnapshot();
        return false;
    }

    const bool bSuccess = ApplyTerrainFromTilesInternal(
        VoxelWorld,
        WorldTiles,
        GridWidth,
        GridHeight
    );

    if (bSuccess)
    {
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
    }
    else
    {
        ScheduleTerrainRetry();
    }

    return true;
}

bool UVoxelWorldManager::ApplyTerrainFromTilesInternal(
    AVoxelWorld* VoxelWorld,
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld is null.")
        );

        ClearTerrainSnapshot();
        return false;
    }

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: invalid grid size %d x %d."),
            GridWidth,
            GridHeight
        );

        ClearTerrainSnapshot();
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );

        ClearTerrainSnapshot();
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation start (Density Generator). Tiles=%d Grid=%dx%d ChunkSizeVoxels=%d Mode=%s Step=%d"),
        WorldTiles.Num(),
        GridWidth,
        GridHeight,
        GetChunkSizeInVoxels(),
        CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        GetEffectiveBlockHeightStep()
    );

    CacheTerrainMetadata(WorldTiles, GridWidth, GridHeight);
    BuildTerrainSnapshot(WorldTiles, GridWidth, GridHeight);

    if (!HasTerrainSnapshot())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: terrain snapshot build produced invalid state.")
        );
        return false;
    }

#if WITH_VOXEL_PLUGIN
    UWorldForgeTerrainGenerator* Generator = NewObject<UWorldForgeTerrainGenerator>(VoxelWorld);
    if (!Generator)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: could not allocate WorldForge terrain generator.")
        );
        return false;
    }

    Generator->SetTerrainSnapshot(TerrainSnapshot);
    Generator->SetSeed(1337);

    // Legacy voxel-plugin generator path:
    // If your installed plugin exposes different assignment calls,
    // only the block below should need adjustment.
    VoxelWorld->SetGeneratorObject(Generator);

    if (VoxelWorld->IsCreated())
    {
        VoxelWorld->DestroyWorld();
    }

    VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
#else
    UE_LOG(
        LogTemp,
        Error,
        TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: WITH_VOXEL_PLUGIN is disabled.")
    );
    return false;
#endif

    const bool bWorldCreated = VoxelWorld->IsCreated();
    if (!bWorldCreated)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld did not report created after generator assignment.")
        );
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation complete (Density Generator). Applied=%d / %d SnapshotValid=%s"),
        ExpectedTileCount,
        ExpectedTileCount,
        HasTerrainSnapshot() ? TEXT("true") : TEXT("false")
    );

    BroadcastTerrainReadyIfNeeded();
    return true;
}

void UVoxelWorldManager::ScheduleTerrainRetry()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ScheduleTerrainRetry failed: GetWorld() returned null.")
        );
        return;
    }

    if (bTerrainRetryScheduled)
    {
        return;
    }

    bTerrainRetryScheduled = true;

    World->GetTimerManager().SetTimer(
        TerrainRetryTimerHandle,
        this,
        &UVoxelWorldManager::RetryPendingTerrainGeneration,
        TerrainRetryIntervalSeconds,
        false
    );

    if (bLogTerrainRetry)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Scheduled terrain retry in %.2f seconds."),
            TerrainRetryIntervalSeconds
        );
    }
}

void UVoxelWorldManager::RetryPendingTerrainGeneration()
{
    bTerrainRetryScheduled = false;

    if (PendingWorldTiles.Num() == 0 || PendingGridWidth <= 0 || PendingGridHeight <= 0)
    {
        return;
    }

    if (!IsReady())
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("[VoxelWorldManager] Terrain retry failed: VoxelWorld actor never became ready.")
            );
            ClearPendingTerrainRequest();
            ClearTerrainSnapshot();
            return;
        }

        if (bLogTerrainRetry)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelWorldManager] Terrain retry %d: VoxelWorld actor still not ready."),
                TerrainRetryCount
            );
        }

        ScheduleTerrainRetry();
        return;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("[VoxelWorldManager] Terrain retry failed: cached actor is not AVoxelWorld.")
            );
            ClearPendingTerrainRequest();
            ClearTerrainSnapshot();
            return;
        }

        if (bLogTerrainRetry)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelWorldManager] Terrain retry %d: cached actor is not AVoxelWorld."),
                TerrainRetryCount
            );
        }

        ScheduleTerrainRetry();
        return;
    }

    if (ApplyTerrainFromTilesInternal(VoxelWorld, PendingWorldTiles, PendingGridWidth, PendingGridHeight))
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Pending terrain generation applied successfully on retry %d."),
            TerrainRetryCount
        );
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
        return;
    }

    ++TerrainRetryCount;

    if (TerrainRetryCount > MaxTerrainRetryCount)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] Terrain retry failed: generator terrain application never succeeded.")
        );
        ClearPendingTerrainRequest();
        ClearTerrainSnapshot();
        return;
    }

    ScheduleTerrainRetry();
}

void UVoxelWorldManager::ClearPendingTerrainRequest()
{
    PendingWorldTiles.Reset();
    PendingGridWidth = 0;
    PendingGridHeight = 0;
    TerrainRetryCount = 0;
    bTerrainRetryScheduled = false;
}

void UVoxelWorldManager::BroadcastTerrainReadyIfNeeded()
{
    if (bTerrainReadyBroadcasted)
    {
        return;
    }

    if (!IsTerrainRuntimeStable())
    {
        return;
    }

    bTerrainReadyBroadcasted = true;

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Terrain is ready for gameplay."));

    if (HasCachedTerrainData())
    {
        const FIntPoint CenterCell(
            FMath::Max(0, CachedTerrainGridWidth / 2),
            FMath::Max(0, CachedTerrainGridHeight / 2)
        );

        const FIntPoint TopLeft(0, 0);
        const FIntPoint TopRight(FMath::Max(0, CachedTerrainGridWidth - 1), 0);
        const FIntPoint BottomLeft(0, FMath::Max(0, CachedTerrainGridHeight - 1));
        const FIntPoint BottomRight(
            FMath::Max(0, CachedTerrainGridWidth - 1),
            FMath::Max(0, CachedTerrainGridHeight - 1)
        );

        FVector CenterWorld = FVector::ZeroVector;
        FVector TopLeftWorld = FVector::ZeroVector;
        FVector TopRightWorld = FVector::ZeroVector;
        FVector BottomLeftWorld = FVector::ZeroVector;
        FVector BottomRightWorld = FVector::ZeroVector;

        const bool bHasCenter = TryGetSurfaceWorldLocation(CenterCell, CenterWorld, TerrainSurfaceZOffset);
        const bool bHasTL = TryGetSurfaceWorldLocation(TopLeft, TopLeftWorld, TerrainSurfaceZOffset);
        const bool bHasTR = TryGetSurfaceWorldLocation(TopRight, TopRightWorld, TerrainSurfaceZOffset);
        const bool bHasBL = TryGetSurfaceWorldLocation(BottomLeft, BottomLeftWorld, TerrainSurfaceZOffset);
        const bool bHasBR = TryGetSurfaceWorldLocation(BottomRight, BottomRightWorld, TerrainSurfaceZOffset);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] Terrain samples: CenterCell=(%d,%d) CenterWorld=(%.1f, %.1f, %.1f) TL=(%.1f, %.1f, %.1f) TR=(%.1f, %.1f, %.1f) BL=(%.1f, %.1f, %.1f) BR=(%.1f, %.1f, %.1f)"),
            CenterCell.X,
            CenterCell.Y,
            bHasCenter ? CenterWorld.X : -999999.0f,
            bHasCenter ? CenterWorld.Y : -999999.0f,
            bHasCenter ? CenterWorld.Z : -999999.0f,
            bHasTL ? TopLeftWorld.X : -999999.0f,
            bHasTL ? TopLeftWorld.Y : -999999.0f,
            bHasTL ? TopLeftWorld.Z : -999999.0f,
            bHasTR ? TopRightWorld.X : -999999.0f,
            bHasTR ? TopRightWorld.Y : -999999.0f,
            bHasTR ? TopRightWorld.Z : -999999.0f,
            bHasBL ? BottomLeftWorld.X : -999999.0f,
            bHasBL ? BottomLeftWorld.Y : -999999.0f,
            bHasBL ? BottomLeftWorld.Z : -999999.0f,
            bHasBR ? BottomRightWorld.X : -999999.0f,
            bHasBR ? BottomRightWorld.Y : -999999.0f,
            bHasBR ? BottomRightWorld.Z : -999999.0f
        );

        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

            const double DistanceToCenter = bHasCenter
                ? FVector::Dist(ViewLocation, CenterWorld)
                : -1.0;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelWorldManager] Player view at terrain-ready: View=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f) DistToCenter=%.1f"),
                ViewLocation.X,
                ViewLocation.Y,
                ViewLocation.Z,
                ViewRotation.Pitch,
                ViewRotation.Yaw,
                ViewRotation.Roll,
                DistanceToCenter
            );
        }
    }

    OnTerrainReady.Broadcast();
}
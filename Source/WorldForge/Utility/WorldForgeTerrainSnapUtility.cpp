#include "WorldForgeTerrainSnapUtility.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "VoxelWorldManager.h"

bool UWorldForgeTerrainSnapUtility::TryGetTerrainSnapLocation(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    FVector& OutSnappedLocation,
    float ZOffset,
    float TraceUpDistance,
    float TraceDownDistance,
    TEnumAsByte<ECollisionChannel> TraceChannel
)
{
    OutSnappedLocation = InWorldLocation;

    if (!WorldContextObject)
    {
        return false;
    }

    // Preferred path: use voxel terrain data first.
    if (TryGetSnapLocationFromVoxelWorld(WorldContextObject, InWorldLocation, OutSnappedLocation, ZOffset))
    {
        return true;
    }

    // Fallback path: physics trace against rendered/collidable terrain.
    FHitResult Hit;
    if (TryLineTraceTerrain(
        WorldContextObject,
        InWorldLocation,
        Hit,
        TraceUpDistance,
        TraceDownDistance,
        TraceChannel))
    {
        OutSnappedLocation = Hit.ImpactPoint + FVector(0.0f, 0.0f, ZOffset);
        return true;
    }

    return false;
}

bool UWorldForgeTerrainSnapUtility::SnapActorToTerrain(
    UObject* WorldContextObject,
    AActor* Actor,
    float ZOffset,
    bool bAlignToSurfaceNormal,
    float TraceUpDistance,
    float TraceDownDistance,
    TEnumAsByte<ECollisionChannel> TraceChannel
)
{
    if (!WorldContextObject || !Actor)
    {
        return false;
    }

    FVector SnappedLocation = Actor->GetActorLocation();
    if (!TryGetTerrainSnapLocation(
        WorldContextObject,
        Actor->GetActorLocation(),
        SnappedLocation,
        ZOffset,
        TraceUpDistance,
        TraceDownDistance,
        TraceChannel))
    {
        return false;
    }

    FRotator FinalRotation = Actor->GetActorRotation();

    if (bAlignToSurfaceNormal)
    {
        FHitResult Hit;
        if (TryLineTraceTerrain(
            WorldContextObject,
            SnappedLocation,
            Hit,
            TraceUpDistance,
            TraceDownDistance,
            TraceChannel))
        {
            const FRotator SurfaceRotation = FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator();
            FinalRotation = FRotator(
                SurfaceRotation.Pitch,
                FinalRotation.Yaw,
                SurfaceRotation.Roll
            );
        }
    }

    Actor->SetActorLocationAndRotation(SnappedLocation, FinalRotation);
    return true;
}

bool UWorldForgeTerrainSnapUtility::TryLineTraceTerrain(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    FHitResult& OutHit,
    float TraceUpDistance,
    float TraceDownDistance,
    TEnumAsByte<ECollisionChannel> TraceChannel
)
{
    OutHit = FHitResult();

    if (!WorldContextObject)
    {
        return false;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return false;
    }

    const FVector TraceStart = InWorldLocation + FVector(0.0f, 0.0f, FMath::Max(0.0f, TraceUpDistance));
    const FVector TraceEnd = InWorldLocation - FVector(0.0f, 0.0f, FMath::Max(0.0f, TraceDownDistance));

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WorldForgeTerrainSnapTrace), true);
    QueryParams.bTraceComplex = false;

    if (const AActor* IgnoreActor = Cast<AActor>(WorldContextObject))
    {
        QueryParams.AddIgnoredActor(IgnoreActor);
    }

    return World->LineTraceSingleByChannel(
        OutHit,
        TraceStart,
        TraceEnd,
        TraceChannel,
        QueryParams
    );
}

bool UWorldForgeTerrainSnapUtility::TryGetSnapLocationFromVoxelWorld(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    FVector& OutSnappedLocation,
    float ZOffset
)
{
    if (!WorldContextObject)
    {
        return false;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return false;
    }

    const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelWorldManager)
    {
        return false;
    }

    constexpr int32 SearchRadius = 8;
    FIntPoint CubeResolvedCell;
    if (VoxelWorldManager->TryGetCubeSnapForWorldPos(
        InWorldLocation,
        OutSnappedLocation,
        CubeResolvedCell,
        SearchRadius,
        ZOffset))
    {
        return true;
    }

    // Use exact voxel XY -> terrain surface voxel Z when possible.
    const FIntPoint VoxelXY = VoxelWorldManager->WorldToGrid(InWorldLocation);

    int32 SurfaceVoxelZ = 0;
    if (VoxelWorldManager->TryGetTerrainSurfaceVoxelZ(VoxelXY, SurfaceVoxelZ))
    {
        OutSnappedLocation = VoxelWorldManager->GridToWorld(VoxelXY, SurfaceVoxelZ) + FVector(0.0f, 0.0f, ZOffset);
        return true;
    }

    // Fallback to cached terrain cell/chunk surface query.
    const FIntPoint TerrainCell = VoxelWorldManager->WorldToTerrainCell(InWorldLocation);
    const FIntPoint ClampedTerrainCell = VoxelWorldManager->ClampTerrainCellToBounds(TerrainCell);

    if (VoxelWorldManager->TryGetSurfaceWorldLocation(ClampedTerrainCell, OutSnappedLocation, ZOffset))
    {
        return true;
    }

    FIntPoint FallbackCell = ClampedTerrainCell;
    if (VoxelWorldManager->FindNearestSpawnableTerrainCell(ClampedTerrainCell, FallbackCell))
    {
        if (VoxelWorldManager->TryGetSurfaceWorldLocation(FallbackCell, OutSnappedLocation, ZOffset))
        {
            return true;
        }
    }

    return false;
}
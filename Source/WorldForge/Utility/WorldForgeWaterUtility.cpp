#include "Utility/WorldForgeWaterUtility.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Voxel/VoxelWorldManager.h"

float UWorldForgeWaterUtility::FallbackGlobalWaterPlaneZ = 0.0f;

bool UWorldForgeWaterUtility::TryGetWaterSurfaceZ(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    float& OutWaterSurfaceZ,
    EWorldForgeWaterBodyType& OutWaterBodyType,
    AActor*& OutWaterActor
)
{
    OutWaterSurfaceZ = 0.0f;
    OutWaterBodyType = EWorldForgeWaterBodyType::None;
    OutWaterActor = nullptr;

    if (!WorldContextObject)
    {
        return false;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return false;
    }

    if (TryFindTaggedWaterActor(World, InWorldLocation, OutWaterSurfaceZ, OutWaterBodyType, OutWaterActor))
    {
        return true;
    }

    OutWaterSurfaceZ = FallbackGlobalWaterPlaneZ;
    OutWaterBodyType = EWorldForgeWaterBodyType::Unknown;
    OutWaterActor = nullptr;
    return true;
}

bool UWorldForgeWaterUtility::IsWorldLocationUnderWater(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    float DepthTolerance
)
{
    float WaterSurfaceZ = 0.0f;
    EWorldForgeWaterBodyType WaterBodyType = EWorldForgeWaterBodyType::None;
    AActor* WaterActor = nullptr;

    if (!TryGetWaterSurfaceZ(
        WorldContextObject,
        InWorldLocation,
        WaterSurfaceZ,
        WaterBodyType,
        WaterActor))
    {
        return false;
    }

    return InWorldLocation.Z < (WaterSurfaceZ - FMath::Max(0.0f, DepthTolerance));
}

bool UWorldForgeWaterUtility::IsTerrainCellUnderWater(
    UObject* WorldContextObject,
    FIntPoint TerrainCell,
    float DepthTolerance
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

    FVector SurfaceLocation = FVector::ZeroVector;
    if (!VoxelWorldManager->TryGetSurfaceWorldLocation(TerrainCell, SurfaceLocation, 0.0f))
    {
        const FVector CellCenter = VoxelWorldManager->TerrainCellToWorld(TerrainCell, 0.0f, true);
        return IsWorldLocationUnderWater(World, CellCenter, DepthTolerance);
    }

    return IsWorldLocationUnderWater(World, SurfaceLocation, DepthTolerance);
}

bool UWorldForgeWaterUtility::IsActorOverOrUnderWater(
    UObject* WorldContextObject,
    const AActor* Actor,
    float DepthTolerance
)
{
    if (!Actor)
    {
        return false;
    }

    return IsWorldLocationUnderWater(
        WorldContextObject,
        Actor->GetActorLocation(),
        DepthTolerance
    );
}

bool UWorldForgeWaterUtility::IsBuildLocationBlockedByWater(
    UObject* WorldContextObject,
    const FVector& InWorldLocation,
    float DepthTolerance
)
{
    return IsWorldLocationUnderWater(
        WorldContextObject,
        InWorldLocation,
        DepthTolerance
    );
}

void UWorldForgeWaterUtility::SetFallbackGlobalWaterPlaneZ(float InWaterPlaneZ)
{
    FallbackGlobalWaterPlaneZ = InWaterPlaneZ;
}

float UWorldForgeWaterUtility::GetFallbackGlobalWaterPlaneZ()
{
    return FallbackGlobalWaterPlaneZ;
}

bool UWorldForgeWaterUtility::TryFindTaggedWaterActor(
    UWorld* World,
    const FVector& InWorldLocation,
    float& OutWaterSurfaceZ,
    EWorldForgeWaterBodyType& OutWaterBodyType,
    AActor*& OutWaterActor
)
{
    if (!World)
    {
        return false;
    }

    AActor* BestActor = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        if (!ActorLooksLikeWater(Actor))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared2D(Actor->GetActorLocation(), InWorldLocation);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestActor = Actor;
        }
    }

    if (!BestActor)
    {
        return false;
    }

    OutWaterActor = BestActor;
    OutWaterSurfaceZ = BestActor->GetActorLocation().Z;
    OutWaterBodyType = InferWaterBodyTypeFromActor(BestActor);
    return true;
}

EWorldForgeWaterBodyType UWorldForgeWaterUtility::InferWaterBodyTypeFromActor(const AActor* Actor)
{
    if (!Actor)
    {
        return EWorldForgeWaterBodyType::None;
    }

    const FString Name = Actor->GetName().ToLower();
    const FString ClassName = Actor->GetClass()->GetName().ToLower();

    const FString Combined = Name + TEXT(" ") + ClassName;

    if (Combined.Contains(TEXT("ocean")))
    {
        return EWorldForgeWaterBodyType::Ocean;
    }

    if (Combined.Contains(TEXT("lake")))
    {
        return EWorldForgeWaterBodyType::Lake;
    }

    if (Combined.Contains(TEXT("river")))
    {
        return EWorldForgeWaterBodyType::River;
    }

    return EWorldForgeWaterBodyType::Unknown;
}

bool UWorldForgeWaterUtility::ActorLooksLikeWater(const AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }

    if (Actor->ActorHasTag(FName(TEXT("WorldForgeWater"))) ||
        Actor->ActorHasTag(FName(TEXT("Water"))) ||
        Actor->ActorHasTag(FName(TEXT("Ocean"))) ||
        Actor->ActorHasTag(FName(TEXT("Lake"))) ||
        Actor->ActorHasTag(FName(TEXT("River"))))
    {
        return true;
    }

    const FString Name = Actor->GetName().ToLower();
    const FString ClassName = Actor->GetClass()->GetName().ToLower();
    const FString Combined = Name + TEXT(" ") + ClassName;

    return
        Combined.Contains(TEXT("waterline")) ||
        Combined.Contains(TEXT("water")) ||
        Combined.Contains(TEXT("ocean")) ||
        Combined.Contains(TEXT("lake")) ||
        Combined.Contains(TEXT("river"));
}
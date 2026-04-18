#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldForgeTerrainSnapUtility.generated.h"

class AActor;

/**
 * Shared terrain snapping utility for colonists, decorations, resources,
 * build ghosts, and placed structures.
 *
 * Snap order:
 * 1) Try VoxelWorldManager terrain query first
 * 2) Fall back to a downward line trace
 */
UCLASS()
class WORLDFORGE_API UWorldForgeTerrainSnapUtility : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "WorldForge|TerrainSnap", meta = (WorldContext = "WorldContextObject"))
    static bool TryGetTerrainSnapLocation(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        FVector& OutSnappedLocation,
        float ZOffset = 0.0f,
        float TraceUpDistance = 5000.0f,
        float TraceDownDistance = 10000.0f,
        TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|TerrainSnap", meta = (WorldContext = "WorldContextObject"))
    static bool SnapActorToTerrain(
        UObject* WorldContextObject,
        AActor* Actor,
        float ZOffset = 0.0f,
        bool bAlignToSurfaceNormal = false,
        float TraceUpDistance = 5000.0f,
        float TraceDownDistance = 10000.0f,
        TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|TerrainSnap", meta = (WorldContext = "WorldContextObject"))
    static bool TryLineTraceTerrain(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        FHitResult& OutHit,
        float TraceUpDistance = 5000.0f,
        float TraceDownDistance = 10000.0f,
        TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility
    );

private:
    static bool TryGetSnapLocationFromVoxelWorld(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        FVector& OutSnappedLocation,
        float ZOffset
    );
};
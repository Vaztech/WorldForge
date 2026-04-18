#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldForgeWaterUtility.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EWorldForgeWaterBodyType : uint8
{
    None,
    Ocean,
    Lake,
    River,
    Unknown
};

UCLASS()
class WORLDFORGE_API UWorldForgeWaterUtility : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static bool TryGetWaterSurfaceZ(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        float& OutWaterSurfaceZ,
        EWorldForgeWaterBodyType& OutWaterBodyType,
        AActor*& OutWaterActor
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static bool IsWorldLocationUnderWater(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        float DepthTolerance = 2.0f
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static bool IsTerrainCellUnderWater(
        UObject* WorldContextObject,
        FIntPoint TerrainCell,
        float DepthTolerance = 2.0f
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static bool IsActorOverOrUnderWater(
        UObject* WorldContextObject,
        const AActor* Actor,
        float DepthTolerance = 2.0f
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static bool IsBuildLocationBlockedByWater(
        UObject* WorldContextObject,
        const FVector& InWorldLocation,
        float DepthTolerance = 2.0f
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water", meta = (WorldContext = "WorldContextObject"))
    static void SetFallbackGlobalWaterPlaneZ(float InWaterPlaneZ);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Water")
    static float GetFallbackGlobalWaterPlaneZ();

private:
    static bool TryFindTaggedWaterActor(
        UWorld* World,
        const FVector& InWorldLocation,
        float& OutWaterSurfaceZ,
        EWorldForgeWaterBodyType& OutWaterBodyType,
        AActor*& OutWaterActor
    );

    static EWorldForgeWaterBodyType InferWaterBodyTypeFromActor(const AActor* Actor);
    static bool ActorLooksLikeWater(const AActor* Actor);

private:
    static float FallbackGlobalWaterPlaneZ;
};
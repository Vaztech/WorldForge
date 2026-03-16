#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "SplineRoadManager.generated.h"

UENUM(BlueprintType)
enum class ESplineRouteType : uint8
{
    Road,
    River,
    TradeRoute,
    Wall
};

UCLASS()
class WORLDFORGE_API USplineRoadManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    int32 BuildRoute(const TArray<FVector>& Waypoints, ESplineRouteType RouteType, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintCallable)
    void DestroyRoute(int32 RouteID);

    UFUNCTION(BlueprintCallable)
    int32 BuildRiver(const TArray<int32>& TileIDs, int32 GridWidth, float TileWorldSize);

    UFUNCTION(BlueprintImplementableEvent)
    void OnBuildRoute(const TArray<FVector>& Waypoints, ESplineRouteType RouteType, int32 RouteID);

    UFUNCTION(BlueprintImplementableEvent)
    void OnDestroyRoute(int32 RouteID);

private:
    int32 NextRouteID = 0;
    TMap<int32, ESplineRouteType> ActiveRoutes;
};
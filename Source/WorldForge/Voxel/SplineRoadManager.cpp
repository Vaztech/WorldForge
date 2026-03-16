#include "SplineRoadManager.h"

void USplineRoadManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

int32 USplineRoadManager::BuildRoute(
    const TArray<FVector>& Waypoints,
    ESplineRouteType RouteType,
    FGameplayTag MaterialTag)
{
    if (Waypoints.Num() < 2)
    {
        return -1;
    }

    const int32 RouteID = NextRouteID++;
    ActiveRoutes.Add(RouteID, RouteType);

    OnBuildRoute(Waypoints, RouteType, RouteID);

    return RouteID;
}

void USplineRoadManager::DestroyRoute(int32 RouteID)
{
    ActiveRoutes.Remove(RouteID);
    OnDestroyRoute(RouteID);
}

int32 USplineRoadManager::BuildRiver(
    const TArray<int32>& TileIDs,
    int32 GridWidth,
    float TileWorldSize)
{
    if (TileIDs.Num() < 2)
    {
        return -1;
    }

    TArray<FVector> Waypoints;
    Waypoints.Reserve(TileIDs.Num());

    for (int32 TileID : TileIDs)
    {
        Waypoints.Add(
            FVector(
                float(TileID % GridWidth) * TileWorldSize,
                float(TileID / GridWidth) * TileWorldSize,
                0.f
            )
        );
    }

    return BuildRoute(Waypoints, ESplineRouteType::River, FGameplayTag());
}
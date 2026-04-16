#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/WorldDecorationTypes.h"
#include "WorldDecorationSubsystem.generated.h"

class AActor;
class UNavigationContext;
class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API UWorldDecorationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UWorldDecorationSubsystem();

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Decoration")
    bool GenerateDecorations(
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight,
        int64 WorldSeed
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Decoration")
    void ClearGeneratedDecorations();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Decoration")
    const TArray<FWorldDecorationSpawnRecord>& GetGeneratedRecords() const
    {
        return GeneratedRecords;
    }

private:
    void RegisterDefaultEntries();

    bool IsCandidateNearAllowedBiome(
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight,
        FIntPoint AnchorCell,
        const FWorldDecorationEntry& Entry
    ) const;

    bool PassesAnchorTerrainRules(
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight,
        FIntPoint AnchorCell,
        const FWorldDecorationEntry& Entry
    ) const;

    bool PassesDistributionRules(
        FIntPoint AnchorCell,
        int64 WorldSeed,
        const FWorldDecorationEntry& Entry
    ) const;

    bool TryEvaluatePlacement(
        UVoxelWorldManager* VoxelManager,
        int32 GridWidth,
        int32 GridHeight,
        FIntPoint AnchorCell,
        const FWorldDecorationEntry& Entry,
        FVector& OutWorldLocation,
        TArray<FIntPoint>& OutOccupiedWorldCells
    ) const;

    void ApplyNavigationBlocking(
        UNavigationContext* NavigationContext,
        const TArray<FIntPoint>& OccupiedWorldCells
    );

private:
    UPROPERTY(Transient)
    TArray<FWorldDecorationEntry> DefaultEntries;

    UPROPERTY(Transient)
    TArray<FWorldDecorationSpawnRecord> GeneratedRecords;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> SpawnedActors;

    bool bDefaultsRegistered = false;
};
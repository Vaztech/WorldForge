#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "SettlementManager.generated.h"

USTRUCT(BlueprintType)
struct FWorldForgeSettlementSeedInventory
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Settlement|Seeds")
    TMap<FName, int32> CropSeedQuantities;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Settlement|Seeds")
    TArray<FName> CropSeedOrder;
};

UCLASS(BlueprintType)
class WORLDFORGE_API USettlementManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 CreateSettlement(const FString& Name, int32 WorldTileID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool SettlementExists(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool AddToStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool ConsumeFromStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 GetStockpileAmount(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool HasStockpile(int32 ID, const FGameplayTag& Tag, int32 Min) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Seeds")
    bool AddStartingSeedInventory(int32 ID, FName CropRowName, int32 Amount);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Seeds")
    int32 GetStartingSeedInventoryAmount(int32 ID, FName CropRowName) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Seeds")
    TMap<FName, int32> GetStartingSeedInventory(int32 ID) const;

    TArray<FName> GetStartingSeedInventoryOrder(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Seeds")
    bool ConsumeStartingSeedInventory(int32 SettlementID, FName CropRowName, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void AddBuilding(int32 ID, const FGameplayTag& Tag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool HasBuilding(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    TArray<FGameplayTag> GetBuildings(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    FSettlementState GetSettlementState(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    TArray<int32> GetAllSettlementIDs() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void SetPlayerCurrency(int32 SettlementID, FGameplayTag CurrencyTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Claims")
    bool AddClaimedWorldTileToSettlement(int32 SettlementID, int32 WorldTileID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool DoesSettlementClaimWorldTile(int32 SettlementID, int32 WorldTileID) const;

    const TArray<int32>* GetClaimedWorldTilesForSettlement(int32 SettlementID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Claims")
    bool CanPlayerClaimWorldTile(int32 WorldTileID, FString& OutReason) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Claims")
    bool ClaimWorldTileForPlayerSettlement(int32 WorldTileID, FString& OutReason);

    bool AddInitialPlayerClaimFootprint(int32 SettlementID, int32 CenterTileX, int32 CenterTileY, int32 Radius = 4);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool IsWorldTileAdjacentToPlayerClaim(int32 WorldTileID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    int32 GetPlayerLandClaimCost(int32 WorldTileID) const;

    bool DoesSettlementClaimVoxelCell(int32 SettlementID, const FIntPoint& VoxelCell) const;
    bool DoesSettlementClaimVoxelCellDetailed(
        int32 SettlementID,
        const FIntPoint& VoxelCell,
        int32& OutWorldTileID,
        FIntPoint& OutWorldTileXY,
        int32& OutClaimedWorldTileCount) const;

    // Strategic claims are stored as world-map tile IDs on FSettlementState.
    // Local tool actions operate in world position, terrain cell, or voxel cell space.
    // These helpers convert action locations back to world-map tile IDs for enforcement.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool DoesPlayerSettlementClaimWorldTile(int32 WorldTileID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool DoesPlayerSettlementClaimWorldPosition(const FVector& WorldPosition) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool DoesPlayerSettlementClaimWorldCell(const FIntPoint& WorldCell) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Claims")
    bool DoesPlayerSettlementClaimVoxelCell(const FIntPoint& VoxelCell) const;

    bool DoesPlayerSettlementClaimVoxelCellDetailed(
        const FIntPoint& VoxelCell,
        int32& OutWorldTileID,
        FIntPoint& OutWorldTileXY,
        int32& OutPlayerSettlementID,
        int32& OutClaimedWorldTileCount) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settlement|Claims")
    bool bLogClaimQueries = false;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    void SetVisualDirty(int32 SettlementID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    bool IsVisualDirty(int32 SettlementID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    void ClearVisualDirty(int32 SettlementID);

    FSettlementState* GetMutableSettlement(int32 ID);
    const FSettlementState* GetSettlement(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void IncrementPopulation(int32 ID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void IncrementRacePopulation(int32 ID, FGameplayTag RaceTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void DecrementRacePopulation(int32 ID, FGameplayTag RaceTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    TMap<FGameplayTag, int32> GetRacePopulation(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void DecrementPopulation(int32 ID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 GetPopulation(int32 ID) const;


    /** Player-owned settlement. Player build/mine/farm tools must use this, not settlement 0. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Player")
    void SetPlayerSettlementID(int32 SettlementID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Player")
    int32 GetPlayerSettlementID() const { return PlayerSettlementID; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Settlement|Player")
    bool HasPlayerSettlement() const { return PlayerSettlementID >= 0 && SettlementExists(PlayerSettlementID); }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Player")
    void ClearPlayerSettlementID();

    const TArray<FSettlementState>& GetAllSettlements() const { return Settlements; }
    void RestoreFromSave(const TArray<FSettlementState>& Saved);

    // TEMP DEBUG HELPERS
    // NOTE: These are temporary testing utilities for resource injection.
    // Reminder: revert/remove before shipping or when proper debug tooling exists.
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "WorldForge|Settlement|Debug")
    void Add10Stone(int32 SettlementID);

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "WorldForge|Settlement|Debug")
    void Add100All(int32 SettlementID);

private:
    TArray<FSettlementState> Settlements;
    int32 NextSettlementID = 0;

    UPROPERTY(Transient)
    TMap<int32, FWorldForgeSettlementSeedInventory> TemporarySeedInventories;

    // -1 until the player colony is created. NPC civ settlements must never set this.
    int32 PlayerSettlementID = INDEX_NONE;

    UPROPERTY(Transient)
    TSet<int32> VisualDirtySettlements;
};

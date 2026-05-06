// Source/WorldForge/Farming/WildHarvestAdapter.h
//
// Sole C++↔Blueprint bridge for the NV3D Wild Harvest pack.
// Mirrors the ShatterStoneAdapter pattern used for mining:
//   - Wild Harvest BPs are NEVER modified
//   - All interaction via WH_Interface_* Blueprint Interfaces
//   - Growth logic stays in BP; C++ only polls state
//
// ── Asset Discovery Results (May 2026) ──
//
// Content paths:
//   /Game/WildHarvest/_Core/Blueprints/BP_CropPatch_Master
//   /Game/WildHarvest/_Core/Blueprints/BP_CropPickup_Master
//   /Game/WildHarvest/_Core/Blueprints/E_CropStates           (BP enum)
//   /Game/WildHarvest/_Core/Blueprints/STR_CropConfig          (BP struct)
//   /Game/WildHarvest/_Core/Blueprints/Interfaces/
//       BPI_WH_Fertilize, BPI_WH_Harvesting, BPI_WH_Interact, BPI_WH_Watering
//   /Game/WildHarvest/WH_LowPoly/Blueprints/
//       BP_CropPatch_LowPoly, DT_LowPoly, E_LowPoly
//
// BP_CropPatch_Master components:
//   DefaultSceneRoot → Crop Collider Sphere, SM_Crop, SM_SoilPatch
//
// BP_CropPatch_Master key variables (read by C++ via property name):
//   "Crop State"         — E_CropStates enum (master lifecycle state)
//   "Growth Stage Index" — int32 (0..4 index into Growth Stages mesh array)
//   "Water Level"        — float
//   "Is fertilized"      — bool
//   "Is Droughted"       — bool
//   "Growth Timer"       — float (seconds remaining in current stage)
//   "Drop Amount"        — int32
//   "Crop Data Table"    — UDataTable* ref (set on spawn)
//   "Crop Row Name"      — FName (set on spawn, e.g. "LP_Wheat")
//
// BP_CropPatch_Master interfaces implemented:
//   WH_Interface_Interact   → BPI_WH_Interact
//   WH_Interface_Water      → BPI_WH_Watering
//   WH_Interface_Harvest    → BPI_WH_Harvesting
//   WH_Interface_Fertilize  → BPI_WH_Fertilize
//
// STR_CropConfig DT columns (DT_LowPoly, 30 rows):
//   Display Name, Crop Type, Growth Stages[5] (meshes),
//   Ready To Harvest Stage (int), Harvested Bunch (mesh),
//   Soil Patch Planted / Bare (meshes), Dead Material Instance,
//   UI Icon, Grow/Harvest/Collect/Plant SFX (arrays),
//   Base Growth Time (30.0), Drought Time (60.0),
//   Drought Penalty Factor (1.5), Fertilizer Boost Factor (0.5),
//   Drought Resistance (30.0),
//   Min Yield (5), Max Yield (8),
//   Fertilizer Bonus Yield (2), Drought Penalty Yield (2)
// ════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "FarmingTypes.h"
#include "WildHarvestAdapter.generated.h"

class UDataTable;

UCLASS()
class WORLDFORGE_API UWildHarvestAdapter : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Configuration (call once at game start) ────────────────

    /**
     * Register DT_LowPoly (or any sub-pack DT using STR_CropConfig rows).
     * The LowPoly pack uses a single DT for all 30 crops.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void SetCropDataTable(UDataTable* Table);

    /**
     * Set the crop patch Blueprint class to spawn.
     * Use BP_CropPatch_LowPoly for the LowPoly pack.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void SetCropPatchClass(TSubclassOf<AActor> InClass);

    // ─── Spawn / Despawn ────────────────────────────────────────

    /**
     * Spawn a crop patch actor and configure it.
     * Sets the BP's "Crop Data Table" and "Crop Row Name" variables
     * so the master BP loads the correct STR_CropConfig row.
     *
     * @param CropRow     Row name in DT_LowPoly (e.g. "LP_Wheat")
     * @param WorldPos    World-space position
     * @return            Spawned actor, or nullptr on failure
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    AActor* SpawnCropPatch(FName CropRow, FVector WorldPos);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void DespawnCropPatch(AActor* CropActor);

    // ─── BPI Interface Calls ────────────────────────────────────
    // Calls into BP_CropPatch_Master via WH_Interface_* BPIs.
    // Each BPI is a separate Blueprint Interface asset.

    /** WH_Interface_Interact — initial interaction / begin planting. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool InteractWithCrop(AActor* CropActor);

    /** WH_Interface_Water — water the crop, boost growth, clear drought. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool WaterCrop(AActor* CropActor);

    /** WH_Interface_Harvest — harvest the crop (triggers visuals/SFX). */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool HarvestCrop(AActor* CropActor);

    /** WH_Interface_Fertilize — fertilize (applies Fertilizer Boost Factor). */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool FertilizeCrop(AActor* CropActor);

    /**
     * Advance one growth mesh/stage for sim-step driven farming.
     * Uses Wild Harvest's discovered no-param "Next Growth Stage" function,
     * then falls back to incrementing "Growth Stage Index" directly.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool AdvanceCropGrowth(AActor* CropActor);

    // ─── State Polling (reads BP variables by FName) ────────────

    /** Read "Crop State" enum → mapped to ECropGrowthStage. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    ECropGrowthStage GetCropState(AActor* CropActor) const;

    /** Read "Water Level" float [0..1]. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    float GetCropWaterLevel(AActor* CropActor) const;

    /** Read "Is fertilized" bool. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    bool GetIsFertilized(AActor* CropActor) const;

    /** Read "Is Droughted" bool. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    bool GetIsDroughted(AActor* CropActor) const;

    /** Read "Growth Stage Index" int32 (0-based into Growth Stages array). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    int32 GetGrowthStageIndex(AActor* CropActor) const;

    // ─── Yield Calculation ──────────────────────────────────────

    /**
     * Calculate yield using DT values + RNG variance.
     * Formula: base = RandRange(MinYield, MaxYield)
     *          if fertilized: base += FertilizerBonusYield
     *          if droughted:  base -= DroughtPenaltyYield
     * Uses UWorldForgeRNG (Farming branch).
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    int32 CalculateYield(FName CropRow, bool bFertilized, bool bDroughted) const;

    // ─── Item Tag Resolution ────────────────────────────────────

    /** Map crop category → WorldForge item tag for stockpile. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    FGameplayTag GetItemTagForCategory(ECropCategory Category) const;

    /** Map DT row name → crop category (by known roster). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    ECropCategory GetCategoryForCropRow(FName CropRow) const;

    /** Get all row names from the DT (for Build Mode crop dropdown). */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    TArray<FName> GetAllCropRowNames() const;

    /** Is the adapter ready? (DT + class both set) */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    bool IsConfigured() const;

    /** Getter for FarmingSubsystem to access the DT. */
    UDataTable* GetCropDataTable() const { return CropDataTable; }

private:
    UPROPERTY()
    TObjectPtr<UDataTable> CropDataTable;

    UPROPERTY()
    TSubclassOf<AActor> CropPatchClass;

    // ── Cached FName constants for BP variable access ──
    // These must match the exact variable names visible in BP_CropPatch_Master.
    // If the BP renames a variable, update the corresponding FName here.

    static const FName BPVar_CropState;           // "Crop State"
    static const FName BPVar_WaterLevel;           // "Water Level"
    static const FName BPVar_IsFertilized;         // "Is fertilized"
    static const FName BPVar_IsDroughted;          // "Is Droughted"
    static const FName BPVar_GrowthStageIndex;     // "Growth Stage Index"
    static const FName BPVar_CropDataTable;        // "Crop Data Table"
    static const FName BPVar_CropRowName;          // "Crop Row Name"
    static const FName BPVar_DropAmount;           // "Drop Amount"

    // ── Cached FName constants for BPI function calls ──
    // These are the function names inside each WH_Interface_* BPI.
    // Open each BPI in the editor to confirm the exact function name
    // if the calls don't connect.

    static const FName BPIFunc_Interact;
    static const FName BPIFunc_Water;
    static const FName BPIFunc_Harvest;
    static const FName BPIFunc_Fertilize;

    /** Call a parameter-less BPI function on the actor by name. */
    bool CallBPIFunction(AActor* Actor, FName FuncName) const;

    /** Read a uint8/enum BP variable. */
    bool ReadBPByte(AActor* Actor, FName VarName, uint8& OutVal) const;
    /** Read a float BP variable. */
    bool ReadBPFloat(AActor* Actor, FName VarName, float& OutVal) const;
    /** Read a bool BP variable. */
    bool ReadBPBool(AActor* Actor, FName VarName, bool& OutVal) const;
    /** Read an int32 BP variable. */
    bool ReadBPInt(AActor* Actor, FName VarName, int32& OutVal) const;

    /** Classify a crop row name into a category by known roster. */
    static ECropCategory ClassifyCropRow(FName CropRow);
};

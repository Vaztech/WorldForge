// Source/WorldForge/Embark/EmbarkLoadoutSubsystem.h
//
// UEmbarkLoadoutSubsystem
//
// A small Blueprint-friendly embark/loadout manager.
//
// Why this exists:
// - Farming now needs seeds in stockpile before planting can progress.
// - Instead of hardcoding seeds in SettlementManager::CreateSettlement(),
//   this subsystem lets the player/future UI choose what the colony embarks with.
// - This is intentionally separate from SettlementManager. SettlementManager owns
//   stockpiles; EmbarkLoadoutSubsystem only decides what starting items to add.
//
// Future UI flow:
//   1. Open embark screen.
//   2. Call GetAvailableEmbarkOptions().
//   3. Player adds/removes items with AddItemToCurrentLoadout / RemoveItem...
//   4. Show GetRemainingEmbarkPoints().
//   5. After settlement creation, call ApplyCurrentLoadoutToSettlement(SettlementID).
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "EmbarkLoadoutTypes.h"
#include "EmbarkLoadoutSubsystem.generated.h"

class USettlementManager;

UCLASS(BlueprintType)
class WORLDFORGE_API UEmbarkLoadoutSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // ---------------------------------------------------------------------
    // Option catalog
    // ---------------------------------------------------------------------

    /** Returns every known embark option. Use this to populate a future UI menu. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    const TArray<FEmbarkLoadoutOption>& GetAvailableEmbarkOptions() const { return AvailableOptions; }

    /** Returns options filtered by category. Useful for UI tabs. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    TArray<FEmbarkLoadoutOption> GetEmbarkOptionsByCategory(EEmbarkLoadoutCategory Category, bool bIncludeDebug = false) const;

    /** Finds a single option by item tag. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    bool GetEmbarkOptionByTag(FGameplayTag ItemTag, FEmbarkLoadoutOption& OutOption) const;

    /** Allows Blueprint/DataAsset/menu setup to add more options at runtime. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void AddEmbarkOption(const FEmbarkLoadoutOption& Option);

    /** Clears the current option catalog and rebuilds the built-in default options. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void RebuildDefaultEmbarkOptions();

    // ---------------------------------------------------------------------
    // Current selected loadout
    // ---------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void ClearCurrentLoadout();

    /** Sets exact quantity for a chosen item. Quantity <= 0 removes it. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void SetCurrentLoadoutItem(FGameplayTag ItemTag, int32 Quantity);

    /** Adds quantity to the current loadout. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void AddItemToCurrentLoadout(FGameplayTag ItemTag, int32 Quantity);

    /** Removes quantity from the current loadout. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void RemoveItemFromCurrentLoadout(FGameplayTag ItemTag, int32 Quantity);

    /** Current items selected by the player or debug preset. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    const TArray<FEmbarkSelectedItem>& GetCurrentLoadout() const { return CurrentLoadout; }

    /** Calculates the current loadout point cost based on available option bundle costs. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
    int32 GetUsedEmbarkPoints() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
    int32 GetRemainingEmbarkPoints() const { return FMath::Max(0, MaxEmbarkPoints - GetUsedEmbarkPoints()); }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
    int32 GetMaxEmbarkPoints() const { return MaxEmbarkPoints; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    void SetMaxEmbarkPoints(int32 NewMaxPoints) { MaxEmbarkPoints = FMath::Max(0, NewMaxPoints); }

    // ---------------------------------------------------------------------
    // Presets
    // ---------------------------------------------------------------------

    /** Balanced starter loadout for normal testing. Includes category seeds used by current farming logic. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Presets")
    void ResetToDefaultStarterLoadout();

    /** Heavy farming test loadout: lots of each seed/product type from DT_LowPoly crop roster. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Presets")
    void ResetToFarmTestingLoadout();

    /** Applies default starter loadout directly to an existing settlement. Good temporary PIE hook. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Presets")
    bool ApplyDebugStarterLoadout(int32 SettlementID);

    // ---------------------------------------------------------------------
    // Apply to settlement
    // ---------------------------------------------------------------------

    /** Adds the current loadout items to SettlementManager stockpile. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    bool ApplyCurrentLoadoutToSettlement(int32 SettlementID);

    /** Adds an arbitrary list to SettlementManager stockpile. Useful for UI confirmation screens. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
    bool ApplyLoadoutToSettlement(int32 SettlementID, const TArray<FEmbarkSelectedItem>& LoadoutItems);

private:
    UPROPERTY(EditAnywhere, Category = "WorldForge|Embark")
    int32 MaxEmbarkPoints = 100;

    UPROPERTY(Transient)
    TArray<FEmbarkLoadoutOption> AvailableOptions;

    UPROPERTY(Transient)
    TArray<FEmbarkSelectedItem> CurrentLoadout;

private:
    FGameplayTag MakeItemTag(const TCHAR* TagName) const;
    void AddBuiltInOption(const TCHAR* TagName, const TCHAR* DisplayName, const TCHAR* Description, EEmbarkLoadoutCategory Category, int32 QuantityPerBundle, int32 PointCostPerBundle, bool bDebugOnly = false);
    void AddOrMergeCurrentItem(FGameplayTag ItemTag, int32 Quantity, const FText& DisplayName);
    int32 FindCurrentLoadoutIndex(FGameplayTag ItemTag) const;
    USettlementManager* GetSettlementManager() const;
};

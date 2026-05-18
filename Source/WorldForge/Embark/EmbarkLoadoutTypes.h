// Source/WorldForge/Embark/EmbarkLoadoutTypes.h
//
// Dwarf-Fortress-style embark/loadout data types.
//
// Design goals:
// - Data-driven enough for a future interactive embark menu.
// - Simple enough to use immediately for PIE testing.
// - Uses stockpile item tags, so it does NOT duplicate inventory logic.
// - Supports both seed items and final crop/food products.
//
// Example convention used by the default options:
//   Seed item:    WorldForge.Item.Seed.Potato
//   Product item: WorldForge.Item.Crop.Potato
//
// Farming currently also supports category seed tags such as:
//   WorldForge.Item.Seed.Grain
// These are included in the starter loadout so existing planting logic can
// progress while later systems move toward per-crop seeds.
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EmbarkLoadoutTypes.generated.h"

/**
 * High-level UI grouping for embark/loadout options.
 * This is meant for menus: tabs, filters, icons, sorting, etc.
 */
UENUM(BlueprintType)
enum class EEmbarkLoadoutCategory : uint8
{
    BasicSupplies   UMETA(DisplayName = "Basic Supplies"),
    Seeds           UMETA(DisplayName = "Seeds"),
    Crops           UMETA(DisplayName = "Crops / Food"),
    Fertilizer      UMETA(DisplayName = "Fertilizer"),
    Tools           UMETA(DisplayName = "Tools"),
    Animals         UMETA(DisplayName = "Animals"),
    Gear            UMETA(DisplayName = "Gear"),
    Debug           UMETA(DisplayName = "Debug / Testing")
};

/**
 * One purchasable/choosable loadout option shown in the embark menu.
 *
 * Example:
 *   DisplayName       = "Potato Seeds"
 *   ItemTag           = WorldForge.Item.Seed.Potato
 *   QuantityPerBundle = 25
 *   PointCost         = 2
 */
USTRUCT(BlueprintType)
struct FEmbarkLoadoutOption
{
    GENERATED_BODY()

    /** Stockpile item granted when this option is selected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    FGameplayTag ItemTag;

    /** Human-readable name for UI menus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    FText DisplayName;

    /** Helpful text for tooltips. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark", meta = (MultiLine = true))
    FText Description;

    /** UI grouping/filter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    EEmbarkLoadoutCategory Category = EEmbarkLoadoutCategory::BasicSupplies;

    /** How many stockpile units one bundle gives. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark", meta = (ClampMin = "1"))
    int32 QuantityPerBundle = 1;

    /** How many embark points one bundle costs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark", meta = (ClampMin = "0"))
    int32 PointCostPerBundle = 1;

    /** Whether this option should be visible in normal player-facing menus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    bool bVisibleInMenu = true;

    /** Useful for temporary/dev-only options. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    bool bDebugOnly = false;
};

/**
 * Current player selection for one item type.
 *
 * Quantity is final stockpile quantity, not number of bundles. This makes
 * custom UI sliders easier: a menu can show exact values like "Potato Seeds: 50".
 */
USTRUCT(BlueprintType)
struct FEmbarkSelectedItem
{
    GENERATED_BODY()

    /** Stable loadout item id, e.g. FoodRations or Seed.Barley. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    FName ItemId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    FGameplayTag ItemTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark", meta = (ClampMin = "0"))
    int32 Quantity = 0;

    /** Cached display label for menus/debug output. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
    FText DisplayName;
};

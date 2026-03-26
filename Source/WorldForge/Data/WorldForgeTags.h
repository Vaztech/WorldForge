#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldForgeTags.generated.h"

// ====================================================================
// WorldForgeTags -- all gameplay tags used in C++ logic.
// Declare here, define in WorldForgeTags.cpp.
// Never use .ToString().Contains() for tag logic -- use these instead.
// ====================================================================

// ---- Job tags ------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Eat);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Sleep);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Haul);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Cook);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Craft);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Build);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Idle);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Harvest_Food);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Harvest_Wood);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Harvest_Stone);

// ---- Job category parent tags --------------------------------------
// Correct usage:
// TAG_Job_Harvest_Food.MatchesTag(TAG_Job_Harvest) returns true
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Job_Harvest);

// ---- Item tags -----------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Food);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Wood);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Stone);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_IronOre);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_IronBar);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Meal);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Ale);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Plank);

// ---- Skill tags ----------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Farming);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Mining);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Cooking);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Crafting);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Hauling);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Skill_Building);

// ---- Race mechanic tags --------------------------------------------
// ADDED
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Race_Mechanic_PressureLimit);

// ---- Currency tags -------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Currency_Barter);

// ---- Economy narrative pattern tags --------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Economy_Pattern_Famine);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Economy_Pattern_CurrencyFreefall);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Economy_Pattern_Boom);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Economy_Pattern_Monopoly);

// ---- Simulation event tags -----------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_NeedFoodCritical);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_NeedCritical);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_JobAssigned);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_JobCancelled);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_ProductionComplete);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_BuildingComplete);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_ColonistDied);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_ColonistInjured);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_MoraleCrisis);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Sim_Event_StockpileEmpty);

// ADDED
// ---- Building tags -------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Housing_Hut);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Storage_Stockpile);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Production_Kitchen);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Production_Carpenter);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Defense_Barracks);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Spiritual_Shrine);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Building_Economic_MarketStall);

// ====================================================================
// Blueprint Access Library
// ====================================================================

UCLASS()
class WORLDFORGE_API UWorldForgeTagLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // ---- Job tags --------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobEatTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobSleepTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobHaulTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobCookTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobCraftTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobBuildTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobIdleTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobHarvestTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobHarvestFoodTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobHarvestWoodTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Job")
    static FGameplayTag GetJobHarvestStoneTag();

    // ---- Item tags -------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemFoodTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemWoodTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemStoneTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemIronOreTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemIronBarTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemMealTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemAleTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Item")
    static FGameplayTag GetItemPlankTag();

    // ---- Skill tags ------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillFarmingTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillMiningTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillCookingTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillCraftingTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillHaulingTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Skill")
    static FGameplayTag GetSkillBuildingTag();

    // ---- Currency tags ---------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Currency")
    static FGameplayTag GetCurrencyBarterTag();

    // ---- Economy pattern tags --------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Economy")
    static FGameplayTag GetEconomyPatternFamineTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Economy")
    static FGameplayTag GetEconomyPatternCurrencyFreefallTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Economy")
    static FGameplayTag GetEconomyPatternBoomTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Economy")
    static FGameplayTag GetEconomyPatternMonopolyTag();

    // ---- Simulation event tags -------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetNeedFoodCriticalEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetNeedCriticalEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetJobAssignedEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetJobCancelledEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetProductionCompleteEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetBuildingCompleteEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetColonistDiedEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetColonistInjuredEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetMoraleCrisisEventTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Event")
    static FGameplayTag GetStockpileEmptyEventTag();

    // ADDED
    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingHousingHutTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingStorageStockpileTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingProductionKitchenTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingProductionCarpenterTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingDefenseBarracksTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingSpiritualShrineTag();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Tags|Building")
    static FGameplayTag GetBuildingEconomicMarketStallTag();
};
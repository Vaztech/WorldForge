#include "WorldForgeTags.h"

// ---- Job tags ------------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Eat, "WorldForge.Job.Eat");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Sleep, "WorldForge.Job.Sleep");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Haul, "WorldForge.Job.Haul");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Cook, "WorldForge.Job.Cook");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Craft, "WorldForge.Job.Craft");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Build, "WorldForge.Job.Build");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Idle, "WorldForge.Job.Idle");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Harvest_Food, "WorldForge.Job.Harvest.Food");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Harvest_Wood, "WorldForge.Job.Harvest.Wood");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Harvest_Stone, "WorldForge.Job.Harvest.Stone");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Mine, "WorldForge.Job.Mine");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Harvest, "WorldForge.Job.Harvest");

// ---- Item tags -----------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Food, "WorldForge.Item.Food");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Wood, "WorldForge.Item.Wood");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Stone, "WorldForge.Item.Stone");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_IronOre, "WorldForge.Item.IronOre");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_IronBar, "WorldForge.Item.IronBar");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Meal, "WorldForge.Item.Meal");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Ale, "WorldForge.Item.Ale");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Plank, "WorldForge.Item.Plank");

// ---- Skill tags ----------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Farming, "WorldForge.Skill.Farming");
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Mining, "WorldForge.Skill.Mining");
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Cooking, "WorldForge.Skill.Cooking");
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Crafting, "WorldForge.Skill.Crafting");
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Hauling, "WorldForge.Skill.Hauling");
UE_DEFINE_GAMEPLAY_TAG(TAG_Skill_Building, "WorldForge.Skill.Building");

// ---- Race mechanic tags --------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Race_Mechanic_PressureLimit, "WorldForge.Race.Mechanic.PressureLimit");

// ---- Currency tags -------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Currency_Barter, "WorldForge.Currency.Barter");

// ---- Economy narrative pattern tags --------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Economy_Pattern_Famine, "WorldForge.Economy.Pattern.Famine");
UE_DEFINE_GAMEPLAY_TAG(TAG_Economy_Pattern_CurrencyFreefall, "WorldForge.Economy.Pattern.CurrencyFreefall");
UE_DEFINE_GAMEPLAY_TAG(TAG_Economy_Pattern_Boom, "WorldForge.Economy.Pattern.Boom");
UE_DEFINE_GAMEPLAY_TAG(TAG_Economy_Pattern_Monopoly, "WorldForge.Economy.Pattern.Monopoly");

// ---- Simulation event tags -----------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_NeedFoodCritical, "WorldForge.Sim.Event.NeedFoodCritical");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_NeedCritical, "WorldForge.Sim.Event.NeedCritical");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_JobAssigned, "WorldForge.Sim.Event.JobAssigned");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_JobCancelled, "WorldForge.Sim.Event.JobCancelled");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_ProductionComplete, "WorldForge.Sim.Event.ProductionComplete");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_BuildingComplete, "WorldForge.Sim.Event.BuildingComplete");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_ColonistDied, "WorldForge.Sim.Event.ColonistDied");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_ColonistInjured, "WorldForge.Sim.Event.ColonistInjured");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_MoraleCrisis, "WorldForge.Sim.Event.MoraleCrisis");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_StockpileEmpty, "WorldForge.Sim.Event.StockpileEmpty");

// ADDED
// ---- Building tags -------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Housing_Hut, "WorldForge.Building.Housing.Hut");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Storage_Stockpile, "WorldForge.Building.Storage.Stockpile");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Production_Kitchen, "WorldForge.Building.Production.Kitchen");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Production_Carpenter, "WorldForge.Building.Production.Carpenter");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Defense_Barracks, "WorldForge.Building.Defense.Barracks");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Spiritual_Shrine, "WorldForge.Building.Spiritual.Shrine");
UE_DEFINE_GAMEPLAY_TAG(TAG_Building_Economic_MarketStall, "WorldForge.Building.Economic.MarketStall");

// ====================================================================
// UWorldForgeTagLibrary -- Blueprint accessors for native tags
// ====================================================================

// ====================================================================
// Phase 1: Digging & Mining Tags
// ====================================================================

// ---- Tool tags -----------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Tool_Dig, "WorldForge.Tool.Dig");
UE_DEFINE_GAMEPLAY_TAG(TAG_Tool_Mine, "WorldForge.Tool.Mine");
UE_DEFINE_GAMEPLAY_TAG(TAG_Tool_Pickaxe, "WorldForge.Tool.Pickaxe");

// ---- Terrain block type tags ---------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Dirt, "WorldForge.Terrain.Dirt");
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Stone, "WorldForge.Terrain.Stone");
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Gravel, "WorldForge.Terrain.Gravel");
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Clay, "WorldForge.Terrain.Clay");
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Sand, "WorldForge.Terrain.Sand");
UE_DEFINE_GAMEPLAY_TAG(TAG_Terrain_Bedrock, "WorldForge.Terrain.Bedrock");

// ---- Depth tier tags -----------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_DepthTier_Surface, "WorldForge.DepthTier.Surface");
UE_DEFINE_GAMEPLAY_TAG(TAG_DepthTier_Shallow, "WorldForge.DepthTier.Shallow");
UE_DEFINE_GAMEPLAY_TAG(TAG_DepthTier_Mid, "WorldForge.DepthTier.Mid");
UE_DEFINE_GAMEPLAY_TAG(TAG_DepthTier_Deep, "WorldForge.DepthTier.Deep");
UE_DEFINE_GAMEPLAY_TAG(TAG_DepthTier_Abyss, "WorldForge.DepthTier.Abyss");

// ---- Resource tags -------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Stone, "WorldForge.Resource.Stone");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Iron, "WorldForge.Resource.Iron");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Copper, "WorldForge.Resource.Copper");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Coal, "WorldForge.Resource.Coal");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Tin, "WorldForge.Resource.Tin");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Silver, "WorldForge.Resource.Silver");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Gold, "WorldForge.Resource.Gold");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Quartz, "WorldForge.Resource.Quartz");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Mithril, "WorldForge.Resource.Mithril");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Obsidian, "WorldForge.Resource.Obsidian");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_VoidCrystal, "WorldForge.Resource.VoidCrystal");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Starstone, "WorldForge.Resource.Starstone");

// ---- Batch 3: Resource Discovery category tags ---------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Crystal, "WorldForge.Resource.Crystal");
UE_DEFINE_GAMEPLAY_TAG(TAG_Resource_Fossil, "WorldForge.Resource.Fossil");

// ════════════════════════════════════════════════════════════════════
// Wild Harvest Integration — Farming Tags
// ════════════════════════════════════════════════════════════════════

// ---- Farming job tags --------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_PrepSoil,  "WorldForge.Job.Farm.PrepSoil");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_Plant,     "WorldForge.Job.Farm.Plant");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_Water,     "WorldForge.Job.Farm.Water");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_Harvest,   "WorldForge.Job.Farm.Harvest");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_Clear,     "WorldForge.Job.Farm.Clear");
UE_DEFINE_GAMEPLAY_TAG(TAG_Job_Farm_Fertilize, "WorldForge.Job.Farm.Fertilize");

// ---- Farming item tags -------------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Grain,     "WorldForge.Item.Grain");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Berries,   "WorldForge.Item.Berries");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_RootVeg,   "WorldForge.Item.RootVeg");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Greens,    "WorldForge.Item.Greens");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Herbs,     "WorldForge.Item.Herbs");
UE_DEFINE_GAMEPLAY_TAG(TAG_Item_Mushrooms, "WorldForge.Item.Mushrooms");

// ---- Farming sim event tags --------------------------------------------
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_CropHarvested, "WorldForge.Sim.Event.CropHarvested");
UE_DEFINE_GAMEPLAY_TAG(TAG_Sim_Event_CropDied,      "WorldForge.Sim.Event.CropDied");

// ---- Job tags ------------------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetJobEatTag()
{
    return TAG_Job_Eat;
}

FGameplayTag UWorldForgeTagLibrary::GetJobSleepTag()
{
    return TAG_Job_Sleep;
}

FGameplayTag UWorldForgeTagLibrary::GetJobHaulTag()
{
    return TAG_Job_Haul;
}

FGameplayTag UWorldForgeTagLibrary::GetJobCookTag()
{
    return TAG_Job_Cook;
}

FGameplayTag UWorldForgeTagLibrary::GetJobCraftTag()
{
    return TAG_Job_Craft;
}

FGameplayTag UWorldForgeTagLibrary::GetJobBuildTag()
{
    return TAG_Job_Build;
}

FGameplayTag UWorldForgeTagLibrary::GetJobIdleTag()
{
    return TAG_Job_Idle;
}

FGameplayTag UWorldForgeTagLibrary::GetJobHarvestTag()
{
    return TAG_Job_Harvest;
}

FGameplayTag UWorldForgeTagLibrary::GetJobHarvestFoodTag()
{
    return TAG_Job_Harvest_Food;
}

FGameplayTag UWorldForgeTagLibrary::GetJobHarvestWoodTag()
{
    return TAG_Job_Harvest_Wood;
}

FGameplayTag UWorldForgeTagLibrary::GetJobHarvestStoneTag()
{
    return TAG_Job_Harvest_Stone;
}

FGameplayTag UWorldForgeTagLibrary::GetJobMineTag()
{
    return TAG_Job_Mine;
}

// ---- Item tags -----------------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetItemFoodTag()
{
    return TAG_Item_Food;
}

FGameplayTag UWorldForgeTagLibrary::GetItemWoodTag()
{
    return TAG_Item_Wood;
}

FGameplayTag UWorldForgeTagLibrary::GetItemStoneTag()
{
    return TAG_Item_Stone;
}

FGameplayTag UWorldForgeTagLibrary::GetItemIronOreTag()
{
    return TAG_Item_IronOre;
}

FGameplayTag UWorldForgeTagLibrary::GetItemIronBarTag()
{
    return TAG_Item_IronBar;
}

FGameplayTag UWorldForgeTagLibrary::GetItemMealTag()
{
    return TAG_Item_Meal;
}

FGameplayTag UWorldForgeTagLibrary::GetItemAleTag()
{
    return TAG_Item_Ale;
}

FGameplayTag UWorldForgeTagLibrary::GetItemPlankTag()
{
    return TAG_Item_Plank;
}

// ---- Skill tags ----------------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetSkillFarmingTag()
{
    return TAG_Skill_Farming;
}

FGameplayTag UWorldForgeTagLibrary::GetSkillMiningTag()
{
    return TAG_Skill_Mining;
}

FGameplayTag UWorldForgeTagLibrary::GetSkillCookingTag()
{
    return TAG_Skill_Cooking;
}

FGameplayTag UWorldForgeTagLibrary::GetSkillCraftingTag()
{
    return TAG_Skill_Crafting;
}

FGameplayTag UWorldForgeTagLibrary::GetSkillHaulingTag()
{
    return TAG_Skill_Hauling;
}

FGameplayTag UWorldForgeTagLibrary::GetSkillBuildingTag()
{
    return TAG_Skill_Building;
}

// ---- Currency tags -------------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetCurrencyBarterTag()
{
    return TAG_Currency_Barter;
}

// ---- Economy pattern tags ------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetEconomyPatternFamineTag()
{
    return TAG_Economy_Pattern_Famine;
}

FGameplayTag UWorldForgeTagLibrary::GetEconomyPatternCurrencyFreefallTag()
{
    return TAG_Economy_Pattern_CurrencyFreefall;
}

FGameplayTag UWorldForgeTagLibrary::GetEconomyPatternBoomTag()
{
    return TAG_Economy_Pattern_Boom;
}

FGameplayTag UWorldForgeTagLibrary::GetEconomyPatternMonopolyTag()
{
    return TAG_Economy_Pattern_Monopoly;
}

// ---- Simulation event tags -----------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetNeedFoodCriticalEventTag()
{
    return TAG_Sim_Event_NeedFoodCritical;
}

FGameplayTag UWorldForgeTagLibrary::GetNeedCriticalEventTag()
{
    return TAG_Sim_Event_NeedCritical;
}

FGameplayTag UWorldForgeTagLibrary::GetJobAssignedEventTag()
{
    return TAG_Sim_Event_JobAssigned;
}

FGameplayTag UWorldForgeTagLibrary::GetJobCancelledEventTag()
{
    return TAG_Sim_Event_JobCancelled;
}

FGameplayTag UWorldForgeTagLibrary::GetProductionCompleteEventTag()
{
    return TAG_Sim_Event_ProductionComplete;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingCompleteEventTag()
{
    return TAG_Sim_Event_BuildingComplete;
}

FGameplayTag UWorldForgeTagLibrary::GetColonistDiedEventTag()
{
    return TAG_Sim_Event_ColonistDied;
}

FGameplayTag UWorldForgeTagLibrary::GetColonistInjuredEventTag()
{
    return TAG_Sim_Event_ColonistInjured;
}

FGameplayTag UWorldForgeTagLibrary::GetMoraleCrisisEventTag()
{
    return TAG_Sim_Event_MoraleCrisis;
}

FGameplayTag UWorldForgeTagLibrary::GetStockpileEmptyEventTag()
{
    return TAG_Sim_Event_StockpileEmpty;
}

// ADDED
// ---- Building tags -------------------------------------------------
FGameplayTag UWorldForgeTagLibrary::GetBuildingHousingHutTag()
{
    return TAG_Building_Housing_Hut;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingStorageStockpileTag()
{
    return TAG_Building_Storage_Stockpile;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingProductionKitchenTag()
{
    return TAG_Building_Production_Kitchen;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingProductionCarpenterTag()
{
    return TAG_Building_Production_Carpenter;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingDefenseBarracksTag()
{
    return TAG_Building_Defense_Barracks;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingSpiritualShrineTag()
{
    return TAG_Building_Spiritual_Shrine;
}

FGameplayTag UWorldForgeTagLibrary::GetBuildingEconomicMarketStallTag()
{
    return TAG_Building_Economic_MarketStall;
}
// ---- Batch 3: Resource Discovery category tags ---------------------
FGameplayTag UWorldForgeTagLibrary::GetResourceCrystalTag()
{
    return TAG_Resource_Crystal;
}

FGameplayTag UWorldForgeTagLibrary::GetResourceFossilTag()
{
    return TAG_Resource_Fossil;
}

// ════════════════════════════════════════════════════════════════════
// Wild Harvest Integration — Farming Tag Accessors
// ════════════════════════════════════════════════════════════════════

FGameplayTag UWorldForgeTagLibrary::GetJobFarmPrepSoilTag()  { return TAG_Job_Farm_PrepSoil; }
FGameplayTag UWorldForgeTagLibrary::GetJobFarmPlantTag()     { return TAG_Job_Farm_Plant; }
FGameplayTag UWorldForgeTagLibrary::GetJobFarmWaterTag()     { return TAG_Job_Farm_Water; }
FGameplayTag UWorldForgeTagLibrary::GetJobFarmHarvestTag()   { return TAG_Job_Farm_Harvest; }
FGameplayTag UWorldForgeTagLibrary::GetJobFarmClearTag()     { return TAG_Job_Farm_Clear; }
FGameplayTag UWorldForgeTagLibrary::GetJobFarmFertilizeTag() { return TAG_Job_Farm_Fertilize; }

FGameplayTag UWorldForgeTagLibrary::GetItemGrainTag()      { return TAG_Item_Grain; }
FGameplayTag UWorldForgeTagLibrary::GetItemBerriesTag()    { return TAG_Item_Berries; }
FGameplayTag UWorldForgeTagLibrary::GetItemRootVegTag()    { return TAG_Item_RootVeg; }
FGameplayTag UWorldForgeTagLibrary::GetItemGreensTag()     { return TAG_Item_Greens; }
FGameplayTag UWorldForgeTagLibrary::GetItemHerbsTag()      { return TAG_Item_Herbs; }
FGameplayTag UWorldForgeTagLibrary::GetItemMushroomsTag()  { return TAG_Item_Mushrooms; }

FGameplayTag UWorldForgeTagLibrary::GetCropHarvestedEventTag() { return TAG_Sim_Event_CropHarvested; }
FGameplayTag UWorldForgeTagLibrary::GetCropDiedEventTag()      { return TAG_Sim_Event_CropDied; }

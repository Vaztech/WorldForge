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

// ====================================================================
// UWorldForgeTagLibrary -- Blueprint accessors for native tags
// ====================================================================

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
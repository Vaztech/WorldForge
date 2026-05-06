// Source/WorldForge/Farming/FarmingDebugHelper.h
//
// Quick debug functions for testing the farming system in PIE.
// Call these from the console, Blueprint, or CheatManager.
//
// Usage from C++ (e.g. in WorldForgeCheatManager):
//   UFarmingDebugHelper::SpawnTestFarmZone(GetWorld());
//
// Usage from Blueprint:
//   Call "Spawn Test Farm Zone" node (BlueprintCallable)
// ════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FarmingDebugHelper.generated.h"

UCLASS()
class WORLDFORGE_API UFarmingDebugHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Spawns a 3x3 wheat farm zone at grid position (5,5) in settlement 0.
     * Call this in PIE to immediately test the farming job loop.
     *
     * Prerequisites:
     *   - WildHarvestAdapter must be configured (DT + class set)
     *   - Settlement 0 must exist with registered agents
     *   - ProcessorManager must be ticking
     *
     * What happens after calling this:
     *   1. FarmingSubsystem creates a 3x3 zone with 9 tiles
     *   2. Next tick: TickFarmZones() finds 9 empty tiles, posts 9 Plant jobs
     *   3. JobProcessor assigns Plant jobs to idle colonists
     *   4. Colonists walk to tiles, enter Working state
     *   5. ProductionProcessor ticks progress, completes Plant tasks
     *   6. CompleteTask routes to FarmingSubsystem::OnPlantCompleted
     *   7. Adapter spawns BP_CropPatch_LowPoly, calls WH_Interface_Interact
     *   8. Wild Harvest BP starts growing internally
     *   9. Next ticks: adapter polls "Crop State", posts Water/Harvest jobs
     *  10. On harvest: yield deposited to stockpile as TAG_Item_Grain
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Farming",
        meta = (WorldContext = "WorldContextObject"))
    static void SpawnTestFarmZone(UObject* WorldContextObject);

    /**
     * Logs the current state of all farm zones and the adapter config.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Farming",
        meta = (WorldContext = "WorldContextObject"))
    static void LogFarmingStatus(UObject* WorldContextObject);
};

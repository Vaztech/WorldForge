// Source/WorldForge/Farming/FarmingDebugHelper.cpp

#include "Farming/FarmingDebugHelper.h"
#include "Farming/FarmingSubsystem.h"
#include "Farming/WildHarvestAdapter.h"
#include "Farming/FarmingTypes.h"
#include "Engine/World.h"

void UFarmingDebugHelper::SpawnTestFarmZone(UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[FarmingDebug] SpawnTestFarmZone: No world."));
        return;
    }

    UWildHarvestAdapter* Adapter = World->GetSubsystem<UWildHarvestAdapter>();
    UFarmingSubsystem* FarmSub = World->GetSubsystem<UFarmingSubsystem>();

    if (!Adapter)
    {
        UE_LOG(LogTemp, Error, TEXT("[FarmingDebug] WildHarvestAdapter not found."));
        return;
    }

    if (!FarmSub)
    {
        UE_LOG(LogTemp, Error, TEXT("[FarmingDebug] FarmingSubsystem not found."));
        return;
    }

    if (!Adapter->IsConfigured())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[FarmingDebug] Adapter not configured! Set WildHarvest_DT_LowPoly and "
                 "WildHarvestCropPatchLowPolyClass in Project Settings → WorldForge → Farming → WildHarvest."));
        return;
    }

    // Create a 3x3 wheat farm zone at grid (5,5) in settlement 0.
    const int32 ZoneID = FarmSub->CreateFarmZone(
        /*SettlementID=*/ 0,
        /*Origin=*/ FIntPoint(5, 5),
        /*SizeX=*/ 3,
        /*SizeY=*/ 3,
        /*Category=*/ ECropCategory::Grains,
        /*CropRow=*/ FName(TEXT("LP_Wheat")),
        /*bAutoReplant=*/ true);

    if (ZoneID != INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingDebug] ✓ Test farm zone created! ZoneID=%d, 3x3 Wheat at (5,5). "
                 "Watch for [FarmingSubsystem] and [ProductionProcessor] Farm logs on next sim ticks."),
            ZoneID);
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[FarmingDebug] CreateFarmZone returned INDEX_NONE. Check FarmingSubsystem logs."));
    }
}

void UFarmingDebugHelper::LogFarmingStatus(UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[FarmingDebug] LogFarmingStatus: No world."));
        return;
    }

    UWildHarvestAdapter* Adapter = World->GetSubsystem<UWildHarvestAdapter>();
    UFarmingSubsystem* FarmSub = World->GetSubsystem<UFarmingSubsystem>();

    UE_LOG(LogTemp, Warning, TEXT("========== FARMING STATUS =========="));

    // Adapter status
    if (Adapter)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Adapter] Configured=%s DT=%s Class=%s"),
            Adapter->IsConfigured() ? TEXT("YES") : TEXT("NO"),
            Adapter->GetCropDataTable() ? *Adapter->GetCropDataTable()->GetName() : TEXT("null"),
            TEXT("(check via BP)"));

        if (Adapter->IsConfigured())
        {
            TArray<FName> Rows = Adapter->GetAllCropRowNames();
            UE_LOG(LogTemp, Warning, TEXT("[Adapter] Available crops: %d rows"), Rows.Num());
            for (const FName& Row : Rows)
            {
                UE_LOG(LogTemp, Warning, TEXT("  - %s (Category=%d)"),
                    *Row.ToString(),
                    static_cast<int32>(Adapter->GetCategoryForCropRow(Row)));
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Adapter] NOT FOUND"));
    }

    // Zones status
    if (FarmSub)
    {
        const TArray<FWorldForgeFarmZone>& Zones = FarmSub->GetAllZones();
        UE_LOG(LogTemp, Warning, TEXT("[FarmingSubsystem] %d zones"), Zones.Num());

        for (const FWorldForgeFarmZone& Zone : Zones)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("  Zone %d: State=%d Settlement=%d Crop=%s Origin=(%d,%d) Size=%dx%d "
                     "Planted=%d Ready=%d Dead=%d Yield=%d"),
                Zone.ZoneID,
                static_cast<int32>(Zone.ZoneState),
                Zone.SettlementID,
                *Zone.CropRowName.ToString(),
                Zone.Origin.X, Zone.Origin.Y,
                Zone.SizeX, Zone.SizeY,
                Zone.PlantedCount, Zone.ReadyCount, Zone.DeadCount,
                Zone.TotalYieldThisSeason);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[FarmingSubsystem] NOT FOUND"));
    }

    UE_LOG(LogTemp, Warning, TEXT("========== END FARMING STATUS =========="));
}

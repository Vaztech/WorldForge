#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "DebugSimulationSystem.generated.h"

class FSubsystemCollectionBase;

// Debug overlay modes used by the simulation debug UI.
UENUM(BlueprintType)
enum class EDebugOverlay : uint8
{
    None,
    NeedOverlay,
    TaskAssignment,
    ReservationOverlay,
    PathfindingOverlay,
    StockpileInspect,
    BuildingState,
    ColonistInspector,
    JobQueueDisplay,
    MoraleBreakdown,
    IncidentEventLog,
    ProcessorTiming
};

/*
 * CHANGE:
 * Kept the subsystem Blueprint-facing and explicitly Blueprintable/BlueprintType.
 *
 * WHY:
 * This preserves the existing architecture and keeps the debug API visible to Blueprint.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UDebugSimulationSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // =========================================================================
    // Overlay Control
    // =========================================================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Overlay")
    void SetActiveOverlay(EDebugOverlay O);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Overlay")
    void ToggleOverlay(EDebugOverlay O);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Overlay")
    void CycleToNextOverlay();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Overlay")
    EDebugOverlay GetActiveOverlay() const { return ActiveOverlay; }

    // =========================================================================
    // Overlay Data Helpers
    // =========================================================================

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Needs")
    FString GetLowestNeedLabel(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Needs")
    FLinearColor GetNeedColor(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Tasks")
    FString GetAgentTaskLabel(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Reservations")
    TArray<FString> GetReservationSummary(int32 SID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Pathfinding")
    bool ShouldShowNavGrid() const
    {
        return ActiveOverlay == EDebugOverlay::PathfindingOverlay;
    }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Stockpiles")
    TArray<FString> GetStockpileSummary(int32 SID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Buildings")
    FString GetBuildingStateLabel(FGameplayTag Tag, int32 SID) const;

    // =========================================================================
    // Colonist Inspection
    // =========================================================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Inspector")
    void SelectAgentForInspection(int32 AgentID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Inspector")
    FString GetInspectionReport() const;

    // =========================================================================
    // Processor / System Summaries
    // =========================================================================

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Jobs")
    FString GetJobQueueSummary(int32 SID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Morale")
    FString GetMoraleReport(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Events")
    TArray<FString> GetRecentEventLog() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug|Timing")
    TArray<FString> GetProcessorTimingReport() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Debug|Timing")
    void OnSimulationStep(int32 StepNumber, int32 SID);

private:
    // Current active overlay mode.
    EDebugOverlay ActiveOverlay = EDebugOverlay::None;

    // Currently selected colonist for F7-style inspector view.
    int32 SelectedAgentID = -1;

    // Last simulation step received by the debug system.
    int32 LastStepNumber = 0;
};

/*
 * CHANGE:
 * Added a tiny Blueprint function library with one helper:
 * GetDebugSimulationSystem(WorldContextObject)
 *
 * WHY:
 * The auto-generated subsystem getter node is not appearing in your Blueprint search.
 * This gives BP_DebugHUD a guaranteed, explicit way to fetch the subsystem instance
 * without changing the architecture or moving logic out of Blueprint.
 *
 * IMPORTANT:
 * - This is read-only access plumbing only.
 * - It does NOT redesign the subsystem.
 * - It does NOT modify simulation data.
 */
UCLASS()
class WORLDFORGE_API UDebugSimulationBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "WorldForge|Debug", meta = (WorldContext = "WorldContextObject"))
    static UDebugSimulationSystem* GetDebugSimulationSystem(const UObject* WorldContextObject);
};
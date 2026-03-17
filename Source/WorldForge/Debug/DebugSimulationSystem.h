#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
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

UCLASS()
class WORLDFORGE_API UDebugSimulationSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Overlay control
    UFUNCTION(BlueprintCallable) void SetActiveOverlay(EDebugOverlay O);
    UFUNCTION(BlueprintCallable) void ToggleOverlay(EDebugOverlay O);
    UFUNCTION(BlueprintCallable) void CycleToNextOverlay();
    UFUNCTION(BlueprintCallable) EDebugOverlay GetActiveOverlay() const { return ActiveOverlay; }

    // Overlay data helpers
    UFUNCTION(BlueprintCallable) FString GetLowestNeedLabel(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) FLinearColor GetNeedColor(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) FString GetAgentTaskLabel(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetReservationSummary(int32 SID) const;

    // Returns true when nav/path overlay should be rendered.
    UFUNCTION(BlueprintCallable) bool ShouldShowNavGrid() const
    {
        return ActiveOverlay == EDebugOverlay::PathfindingOverlay;
    }

    UFUNCTION(BlueprintCallable) TArray<FString> GetStockpileSummary(int32 SID) const;
    UFUNCTION(BlueprintCallable) FString GetBuildingStateLabel(FGameplayTag Tag, int32 SID) const;

    // Colonist inspection
    UFUNCTION(BlueprintCallable) void SelectAgentForInspection(int32 AgentID);
    UFUNCTION(BlueprintCallable) FString GetInspectionReport() const;

    // Processor/system summaries
    UFUNCTION(BlueprintCallable) FString GetJobQueueSummary(int32 SID) const;
    UFUNCTION(BlueprintCallable) FString GetMoraleReport(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetRecentEventLog() const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetProcessorTimingReport() const;

    // Called externally each simulation step so debug overlays can show
    // current-step timing/event context.
    void OnSimulationStep(int32 StepNumber, int32 SID);

private:
    // Currently active debug overlay.
    EDebugOverlay ActiveOverlay = EDebugOverlay::None;

    // Agent currently selected for detailed inspection.
    int32 SelectedAgentID = -1;

    // Last processed simulation step number for timing/report display.
    int32 LastStepNumber = 0;
};
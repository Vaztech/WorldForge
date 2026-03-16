#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "DebugSimulationSystem.generated.h"

class FSubsystemCollectionBase;

UENUM(BlueprintType)
enum class EDebugOverlay : uint8
{
    None, NeedOverlay, TaskAssignment, ReservationOverlay, PathfindingOverlay,
    StockpileInspect, BuildingState, ColonistInspector, JobQueueDisplay,
    MoraleBreakdown, IncidentEventLog, ProcessorTiming
};

UCLASS()
class WORLDFORGE_API UDebugSimulationSystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable) void SetActiveOverlay(EDebugOverlay O);
    UFUNCTION(BlueprintCallable) void ToggleOverlay(EDebugOverlay O);
    UFUNCTION(BlueprintCallable) void CycleToNextOverlay();
    UFUNCTION(BlueprintCallable) EDebugOverlay GetActiveOverlay() const { return ActiveOverlay; }

    UFUNCTION(BlueprintCallable) FString      GetLowestNeedLabel(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) FLinearColor GetNeedColor(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) FString GetAgentTaskLabel(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetReservationSummary(int32 SID) const;
    UFUNCTION(BlueprintCallable) bool ShouldShowNavGrid() const { return ActiveOverlay == EDebugOverlay::PathfindingOverlay; }
    UFUNCTION(BlueprintCallable) TArray<FString> GetStockpileSummary(int32 SID) const;
    UFUNCTION(BlueprintCallable) FString GetBuildingStateLabel(FGameplayTag Tag, int32 SID) const;
    UFUNCTION(BlueprintCallable) void    SelectAgentForInspection(int32 AgentID);
    UFUNCTION(BlueprintCallable) FString GetInspectionReport() const;
    UFUNCTION(BlueprintCallable) FString GetJobQueueSummary(int32 SID) const;
    UFUNCTION(BlueprintCallable) FString GetMoraleReport(int32 AgentID) const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetRecentEventLog() const;
    UFUNCTION(BlueprintCallable) TArray<FString> GetProcessorTimingReport() const;

    void OnSimulationStep(int32 StepNumber, int32 SID);

private:
    EDebugOverlay ActiveOverlay = EDebugOverlay::None;
    int32         SelectedAgentID = -1;
    int32         LastStepNumber = 0;
};
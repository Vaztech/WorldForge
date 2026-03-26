#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "CommandersDeskManager.generated.h"

UCLASS()
class WORLDFORGE_API UCommandersDeskManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // KEPT EXISTING
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

    // KEPT EXISTING
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // KEPT EXISTING
    UFUNCTION(BlueprintCallable)
    void DrainNarrativeReports();

    // KEPT EXISTING
    // Real H2 event path:
    // listen for flushed sim events and surface narrative-relevant ones
    // to the Commander's Desk log/output path.
    UFUNCTION()
    void OnSimEventFlushed(const FSimulationEvent& Event);

private:
    // ADDED
    void EnqueueNarrativeReport(const FString& Report);

    // ADDED
    TArray<FString> PendingNarrativeReports;
};
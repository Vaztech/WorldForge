#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BaseProcessor.h"
#include "ProcessorManager.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API UProcessorManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override; // Binds to SimTime
    virtual void Deinitialize() override;

    UFUNCTION()
    void RunSimulationStep(int32 StepNumber);

    void RegisterSettlement(int32 ID);
    void RegisterAgent(const FAgentData& Agent);
    void UnregisterAgent(int32 AgentID);

    FAgentData* GetMutableAgent(int32 AgentID);
    const FAgentData* GetAgent(int32 AgentID) const;

    const TArray<FAgentData>& GetAllAgents() const { return Agents; }
    const TArray<float>& GetProcessorTimings() const { return ProcessorTimingsMs; }
    const TArray<FName>& GetProcessorNames() const { return ProcessorNames; }

    void RestoreAgents(const TArray<FAgentData>& Saved);

private:
    void InitializeProcessors();

    TArray<UBaseProcessor*> OrderedProcessors;
    TArray<FAgentData> Agents;
    TArray<int32> ActiveSettlementIDs;
    TArray<float> ProcessorTimingsMs;
    TArray<FName> ProcessorNames;
};
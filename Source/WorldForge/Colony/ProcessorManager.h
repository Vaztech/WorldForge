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

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Processor")
    void RegisterSettlement(int32 ID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Processor")
    void RegisterAgent(const FAgentData& Agent);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Processor")
    void UnregisterAgent(int32 AgentID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Processor")
    void SpawnTestAgent(int32 AgentID, int32 SettlementID, FIntPoint GridPos);

    // Mutable runtime access to an agent by ID.
    FAgentData* GetMutableAgent(int32 AgentID);

    // Read-only runtime access to an agent by ID.
    const FAgentData* GetAgent(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<FAgentData>& GetAllAgents() const { return Agents; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<float>& GetProcessorTimings() const { return ProcessorTimingsMs; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<FName>& GetProcessorNames() const { return ProcessorNames; }

    void RestoreAgents(const TArray<FAgentData>& Saved);

    // Returns the first processor of type T, or nullptr.
    // Used by processors that need to call methods on sibling processors.
    // Example: UJobProcessor calls PM->GetTypedProcessor<UProductionProcessor>()
    template<typename T>
    T* GetTypedProcessor()
    {
        for (UBaseProcessor* Processor : OrderedProcessors)
        {
            if (T* Typed = Cast<T>(Processor))
            {
                return Typed;
            }
        }
        return nullptr;
    }

    template<typename T>
    const T* GetTypedProcessor() const
    {
        for (const UBaseProcessor* Processor : OrderedProcessors)
        {
            if (const T* Typed = Cast<const T>(Processor))
            {
                return Typed;
            }
        }
        return nullptr;
    }

private:
    // Creates and orders all fixed-step processors.
    void InitializeProcessors();

    // Ordered processor list used during fixed-step simulation.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBaseProcessor>> OrderedProcessors;

    // Runtime colony agents tracked by the processor system.
    TArray<FAgentData> Agents;

    // Settlement IDs currently registered with the sim.
    TArray<int32> ActiveSettlementIDs;

    // Per-step timing metrics for debug/profiling.
    TArray<float> ProcessorTimingsMs;

    // Processor display/debug names aligned with OrderedProcessors.
    TArray<FName> ProcessorNames;
};
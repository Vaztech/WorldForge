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
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
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

    FAgentData* GetMutableAgent(int32 AgentID);
    const FAgentData* GetAgent(int32 AgentID) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<FAgentData>& GetAllAgents() const { return Agents; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<float>& GetProcessorTimings() const { return ProcessorTimingsMs; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Processor")
    const TArray<FName>& GetProcessorNames() const { return ProcessorNames; }

    void RestoreAgents(const TArray<FAgentData>& Saved);

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
    void InitializeProcessors();

    UPROPERTY(Transient)
    TArray<TObjectPtr<UBaseProcessor>> OrderedProcessors;

    TArray<FAgentData> Agents;
    TArray<int32> ActiveSettlementIDs;
    TArray<float> ProcessorTimingsMs;
    TArray<FName> ProcessorNames;
};
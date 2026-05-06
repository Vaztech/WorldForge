// MiningProcessor.h

#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "MiningProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UMiningProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Mining"));
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Mining")
    float RequiredMiningProgress = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Simulation|Mining")
    float BaseMiningProgressPerStep = 1.0f;

private:
    bool TryAssignMiningDesignation(
        FAgentData& Agent,
        int32 SettlementID,
        int32 StepNumber,
        UWorld* World);

    void TickMiningWork(
        FAgentData& Agent,
        int32 StepNumber,
        UWorld* World);

    void CompleteMiningDesignation(
        FAgentData& Agent,
        int32 StepNumber,
        UWorld* World);

    bool ConvertMineGridToLocalTarget(
        int32 SettlementID,
        const FIntVector& MineGrid,
        UWorld* World,
        FIntPoint& OutLocalTarget) const;

private:
    UPROPERTY()
    TMap<int32, int32> AgentMiningDesignationIDs;

    UPROPERTY()
    TMap<int32, float> AgentMiningProgress;
};
#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "EconomicProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UEconomicProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // Phase 2: Stub processor.
    // Will handle economy simulation in a later phase.
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    // Debug/display name used by ProcessorManager.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Economic"));
    }
};
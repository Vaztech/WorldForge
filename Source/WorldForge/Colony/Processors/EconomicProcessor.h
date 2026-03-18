#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "WorldForgeDataTypes.h"
#include "EconomicProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UEconomicProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // Phase 3A: Economy processor.
    // Signature confirmed to match UBaseProcessor / existing processors exactly.
    virtual void ProcessStep(
        int32 StepNumber,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World) override;

    // Debug/display name used by ProcessorManager.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Economic"));
    }
};
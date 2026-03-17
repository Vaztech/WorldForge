#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "MoraleProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UMoraleProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // Phase 2: Stub processor.
    // Will handle morale calculations in Phase 3.
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    // Debug/display name used by ProcessorManager.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Morale"));
    }
};
#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "HealthProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UHealthProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // Runs one fixed simulation step for health/injury/death processing
    // for all agents in the specified settlement.
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    // Debug/display name used by the processor manager and profiling UI.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Health"));
    }
};
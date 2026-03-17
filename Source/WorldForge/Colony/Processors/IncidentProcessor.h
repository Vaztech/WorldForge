#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "IncidentProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UIncidentProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    // Phase 2: Stub processor.
    // Will handle dynamic events (raids, disasters, etc.) in Phase 3.
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    // Debug/display name used by ProcessorManager.
    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Incident"));
    }
};
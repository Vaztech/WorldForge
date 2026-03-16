#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "ConstructionProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UConstructionProcessor : public UBaseProcessor
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
        return FName(TEXT("Construction"));
    }
};
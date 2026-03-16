#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "MoraleProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UMoraleProcessor : public UBaseProcessor
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
        return FName(TEXT("Morale"));
    }
};
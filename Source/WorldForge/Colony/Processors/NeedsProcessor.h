#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "NeedsProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UNeedsProcessor : public UBaseProcessor
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
        return FName(TEXT("Needs"));
    }

    float FoodDecay = 0.5f;
    float SleepDecay = 0.3f;
    float SocialDecay = 0.1f;
    float SafetyDecay = 0.05f;
    float PurposeDecay = 0.08f;

    float CriticalThreshold = 20.f;
};
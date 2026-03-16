#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldForgeDataTypes.h"
#include "BaseProcessor.generated.h"

UCLASS(Abstract, BlueprintType)
class WORLDFORGE_API UBaseProcessor : public UObject
{
    GENERATED_BODY()

public:

    virtual void ProcessStep(
        int32 StepNumber,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) PURE_VIRTUAL(UBaseProcessor::ProcessStep, );

    virtual FName GetProcessorName() const
        PURE_VIRTUAL(UBaseProcessor::GetProcessorName, return NAME_None;);

    // Time spent executing last simulation step (milliseconds)
    float LastStepMs = 0.f;
};
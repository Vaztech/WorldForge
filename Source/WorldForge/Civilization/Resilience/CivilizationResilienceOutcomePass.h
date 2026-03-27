#pragma once

#include "CoreMinimal.h"

struct FCivilizationData;
struct FResilienceRecord;
class UWorld;
class UCivilizationResilienceSystem;

class WORLDFORGE_API FCivilizationResilienceOutcomePass
{
public:
    static void ApplyOutcome(
        UCivilizationResilienceSystem* Owner,
        FCivilizationData& Civ,
        FResilienceRecord& Record,
        int32 StepNumber,
        UWorld* World,
        float Health);
};
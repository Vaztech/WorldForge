#include "Resilience/CivilizationResilienceOutcomePass.h"
#include "CivilizationResilienceSystem.h"
#include "WorldForgeDataTypes.h"

void FCivilizationResilienceOutcomePass::ApplyOutcome(
    UCivilizationResilienceSystem* Owner,
    FCivilizationData& Civ,
    FResilienceRecord& Record,
    int32 StepNumber,
    UWorld* World,
    float Health)
{
    if (!Owner) return;

    if (Health < Owner->GetCollapseThreshold())
    {
        Record.ConsecutiveLowHealthTicks++;

        if (Record.ConsecutiveLowHealthTicks >= Owner->GetConsecutiveCollapseTicksRequired())
        {
            Owner->ExecuteCollapse(Civ, Record, StepNumber, World);
        }
    }
    else
    {
        Record.ConsecutiveLowHealthTicks = FMath::Max(0, Record.ConsecutiveLowHealthTicks - 1);
    }
}
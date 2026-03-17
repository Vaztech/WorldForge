#include "MoraleProcessor.h"

void UMoraleProcessor::ProcessStep(
    int32 Step,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World
)
{
    // Phase 2 stub:
    // Morale simulation is intentionally deferred.
    // Keep parameters explicitly unused to avoid warnings while preserving
    // the fixed-step processor interface.
    (void)Step;
    (void)SettlementID;
    (void)Agents;
    (void)World;
}
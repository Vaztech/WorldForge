#include "Culture/Passes/CultureDriftLoggingPass.h"
#include "Culture/CultureDriftContext.h"
#include "Culture/CultureDNAUtils.h"

void FCultureDriftLoggingPass::Run(
    FCultureDriftContext& Context,
    const FCivilizationData& Before,
    const FCivilizationData& After)
{
    for (int32 Axis = 0; Axis < CA_MAX; ++Axis)
    {
        const float BeforeValue = CultureDNAUtils::GetAxisValue(Before.CulturalDNA, Axis);
        const float AfterValue = CultureDNAUtils::GetAxisValue(After.CulturalDNA, Axis);
        const float Delta = FMath::Abs(AfterValue - BeforeValue);

        if (Delta >= 10.0f)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[CultureDriftSystem] Civ %d axis[%d] drifted %.1f -> %.1f at step %d"),
                After.CivID,
                Axis,
                BeforeValue,
                AfterValue,
                Context.StepNumber);
        }
    }
}
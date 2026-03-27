#include "Culture/Passes/CultureDNACapPass.h"
#include "Culture/CultureDriftContext.h"

void FCultureDNACapPass::Run(FCultureDriftContext& Context, FCivilizationData& Civ)
{
    (void)Context;

    Civ.CulturalDNA.HierarchyTolerance = FMath::Clamp(Civ.CulturalDNA.HierarchyTolerance, 0.0f, 100.0f);
    Civ.CulturalDNA.OutgroupHostility = FMath::Clamp(Civ.CulturalDNA.OutgroupHostility, 0.0f, 100.0f);
    Civ.CulturalDNA.LaborEthics = FMath::Clamp(Civ.CulturalDNA.LaborEthics, 0.0f, 100.0f);
    Civ.CulturalDNA.Militarism = FMath::Clamp(Civ.CulturalDNA.Militarism, 0.0f, 100.0f);
    Civ.CulturalDNA.MagicReverence = FMath::Clamp(Civ.CulturalDNA.MagicReverence, 0.0f, 100.0f);
    Civ.CulturalDNA.Individualism = FMath::Clamp(Civ.CulturalDNA.Individualism, 0.0f, 100.0f);
    Civ.CulturalDNA.TheologicalRigidity = FMath::Clamp(Civ.CulturalDNA.TheologicalRigidity, 0.0f, 100.0f);
    Civ.CulturalDNA.EconomicOpenness = FMath::Clamp(Civ.CulturalDNA.EconomicOpenness, 0.0f, 100.0f);
    Civ.CulturalDNA.ExpansionDrive = FMath::Clamp(Civ.CulturalDNA.ExpansionDrive, 0.0f, 100.0f);
    Civ.CulturalDNA.CosmicDevotion = FMath::Clamp(Civ.CulturalDNA.CosmicDevotion, 0.0f, 100.0f);
}
#include "CivCultureSeedPass.h"
#include "../CivilizationGenerator.h"
#include "../../Core/WorldForgeRNG.h"

void FCivCultureSeedPass::Execute(
    const UCivilizationGenerator& Generator,
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG)
{
    // KEPT EXISTING: WorldTiles remains part of the signature to preserve the current driver call shape
    // KEPT EXISTING: No behavior change; logic below is moved directly from CivilizationGenerator::GenerateCulturalDNA
    (void)WorldTiles;

    for (FCivilizationData& Civ : Civs)
    {
        FCultureData& DNA = Civ.CulturalDNA;

        DNA.CivID = Civ.CivID;
        DNA.HierarchyTolerance = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.OutgroupHostility = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.LaborEthics = Generator.GenerateDNAAxis(20.f, 80.f, RNG);
        DNA.Militarism = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.MagicReverence = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.Individualism = Generator.GenerateDNAAxis(20.f, 80.f, RNG);
        DNA.TheologicalRigidity = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.EconomicOpenness = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.ExpansionDrive = Generator.GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.CosmicDevotion = Generator.GenerateDNAAxis(10.f, 90.f, RNG);

        if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))))
        {
            DNA.MagicReverence = FMath::Max(10.f, DNA.MagicReverence - 30.f);
            DNA.EconomicOpenness = FMath::Min(90.f, DNA.EconomicOpenness + 20.f);
        }
        else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))))
        {
            DNA.MagicReverence = FMath::Min(90.f, DNA.MagicReverence + 30.f);
            DNA.HierarchyTolerance = FMath::Min(90.f, DNA.HierarchyTolerance + 20.f);
        }
        else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))))
        {
            DNA.Militarism = FMath::Min(90.f, DNA.Militarism + 25.f);
            DNA.EconomicOpenness = FMath::Max(10.f, DNA.EconomicOpenness - 20.f);
        }
        else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
        {
            DNA.HierarchyTolerance = FMath::Max(10.f, DNA.HierarchyTolerance - 20.f);
            DNA.CosmicDevotion = FMath::Min(90.f, DNA.CosmicDevotion + 20.f);
        }
    }
}
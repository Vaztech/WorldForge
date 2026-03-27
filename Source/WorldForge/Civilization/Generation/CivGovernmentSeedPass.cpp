#include "CivGovernmentSeedPass.h"
#include "../../Core/WorldForgeRNG.h"

void FCivGovernmentSeedPass::Execute(
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG
)
{
    // MODIFIED: Now performs real government seeding using CulturalDNA

    (void)WorldTiles;

    for (FCivilizationData& Civ : Civs)
    {
        const FCultureData& DNA = Civ.CulturalDNA;

        // ADDED: Pull key axes for decision making
        const float Hierarchy = DNA.HierarchyTolerance;
        const float Individualism = DNA.Individualism;
        const float Militarism = DNA.Militarism;
        const float Theology = DNA.TheologicalRigidity;

        FGameplayTag GovernmentTag;

        // ADDED: Basic deterministic branching with slight RNG variation
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_CIV_START);

        if (Hierarchy > 70.f && Individualism < 40.f)
        {
            GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Authoritarian")));
        }
        else if (Individualism > 70.f)
        {
            GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Republic")));
        }
        else if (Militarism > 70.f)
        {
            GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Militaristic")));
        }
        else if (Theology > 70.f)
        {
            GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Theocracy")));
        }
        else
        {
            // ADDED: Slight randomness for fallback variation
            if (R < 0.33f)
            {
                GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Tribal")));
            }
            else if (R < 0.66f)
            {
                GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Council")));
            }
            else
            {
                GovernmentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Chiefdom")));
            }
        }

        // ADDED: Final assignment
        Civ.GovernmentType = GovernmentTag;
    }
}
#include "CivDiplomacySeedPass.h"
#include "../../Core/WorldForgeRNG.h"

namespace
{
    // ADDED: Clamp all seeded diplomacy values to a stable range.
    static float ClampRelationshipScore(const float Value)
    {
        return FMath::Clamp(Value, -100.0f, 100.0f);
    }

    // ADDED: Calculates an initial diplomacy score from already-seeded civ identity.
    static float ComputeInitialRelationshipScore(
        const FCivilizationData& A,
        const FCivilizationData& B,
        UWorldForgeRNG* RNG
    )
    {
        float Score = 0.0f;

        // ADDED: Same race starts with modest familiarity bonus.
        if (A.RaceType.IsValid() && B.RaceType.IsValid())
        {
            Score += (A.RaceType == B.RaceType) ? 12.0f : -8.0f;
        }

        // ADDED: Same government type starts with mild ideological alignment.
        if (A.GovernmentType.IsValid() && B.GovernmentType.IsValid())
        {
            Score += (A.GovernmentType == B.GovernmentType) ? 8.0f : -4.0f;
        }

        // ADDED: Shared currency gives a small trade/integration bonus.
        if (A.CurrencyTag.IsValid() && B.CurrencyTag.IsValid())
        {
            Score += (A.CurrencyTag == B.CurrencyTag) ? 10.0f : 0.0f;
        }

        // ADDED: Openness alignment tends to improve initial relations.
        const float EconomicGap = FMath::Abs(A.CulturalDNA.EconomicOpenness - B.CulturalDNA.EconomicOpenness);
        Score += FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 100.0f), FVector2D(8.0f, -8.0f), EconomicGap);

        // ADDED: Similar hierarchy expectations reduce friction.
        const float HierarchyGap = FMath::Abs(A.CulturalDNA.HierarchyTolerance - B.CulturalDNA.HierarchyTolerance);
        Score += FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 100.0f), FVector2D(5.0f, -5.0f), HierarchyGap);

        // ADDED: Mutual hostility and militarism make relations colder.
        Score -= ((A.CulturalDNA.OutgroupHostility + B.CulturalDNA.OutgroupHostility) * 0.10f);
        Score -= ((A.CulturalDNA.Militarism + B.CulturalDNA.Militarism) * 0.06f);

        // ADDED: Similar cosmic/theological outlook provides a small stabilizing effect.
        const float TheologyGap = FMath::Abs(A.CulturalDNA.TheologicalRigidity - B.CulturalDNA.TheologicalRigidity);
        const float CosmicGap = FMath::Abs(A.CulturalDNA.CosmicDevotion - B.CulturalDNA.CosmicDevotion);
        Score += FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 100.0f), FVector2D(4.0f, -4.0f), TheologyGap);
        Score += FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 100.0f), FVector2D(3.0f, -3.0f), CosmicGap);

        // ADDED: Light deterministic variation so worlds do not feel overly uniform.
        if (RNG != nullptr)
        {
            Score += RNG->NextFloat(UWorldForgeRNG::BRANCH_CIV_START) * 8.0f - 4.0f;
        }

        return ClampRelationshipScore(Score);
    }
}

void FCivDiplomacySeedPass::Execute(
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG
)
{
    // KEPT EXISTING: Signature retains world context for future expansion.
    (void)WorldTiles;

    // MODIFIED: Seed initial diplomacy relationships for every civ pair.
    for (FCivilizationData& Civ : Civs)
    {
        // ADDED: Clear and rebuild to keep generation deterministic and avoid stale data.
        Civ.RelationshipMap.Reset();
    }

    for (int32 i = 0; i < Civs.Num(); ++i)
    {
        for (int32 j = i + 1; j < Civs.Num(); ++j)
        {
            const float RelationshipScore = ComputeInitialRelationshipScore(Civs[i], Civs[j], RNG);

            // ADDED: Seed symmetric starting opinions.
            Civs[i].RelationshipMap.Add(Civs[j].CivID, RelationshipScore);
            Civs[j].RelationshipMap.Add(Civs[i].CivID, RelationshipScore);
        }
    }
}
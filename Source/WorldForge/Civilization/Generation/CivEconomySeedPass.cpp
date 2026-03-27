#include "CivEconomySeedPass.h"
#include "../../Core/WorldForgeRNG.h"

namespace
{
    // ADDED: Safe tag request helper. Returns invalid tag instead of forcing a hard failure
    // if a candidate tag is not registered in the project tag dictionary yet.
    static FGameplayTag TryRequestTag(const TCHAR* TagName)
    {
        return FGameplayTag::RequestGameplayTag(FName(TagName), false);
    }

    // ADDED: Append only valid registered tags so this pass stays runtime-safe.
    static void AddCandidateIfValid(TArray<FGameplayTag>& Candidates, const TCHAR* TagName)
    {
        const FGameplayTag Tag = TryRequestTag(TagName);
        if (Tag.IsValid())
        {
            Candidates.Add(Tag);
        }
    }

    // ADDED: Choose a registered currency tag using live civ/world context.
    static FGameplayTag ChooseCurrencyTagForCiv(
        const FCivilizationData& Civ,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    )
    {
        float TotalCommercePressure = 0.0f;
        int32 ValidTileCount = 0;
        bool bHasCoastalTile = false;
        bool bHasMountainTile = false;
        bool bHasForestTile = false;

        for (const int32 TileID : Civ.ControlledTileIDs)
        {
            if (!WorldTiles.IsValidIndex(TileID))
            {
                continue;
            }

            const FWorldTileData& Tile = WorldTiles[TileID];
            const FString BiomeString = Tile.BiomeType.ToString();

            TotalCommercePressure += Tile.CommercePressure;
            ++ValidTileCount;

            if (BiomeString.Contains(TEXT("Coastal")))
            {
                bHasCoastalTile = true;
            }

            if (BiomeString.Contains(TEXT("Mountain")))
            {
                bHasMountainTile = true;
            }

            if (BiomeString.Contains(TEXT("Forest")))
            {
                bHasForestTile = true;
            }
        }

        const float AverageCommercePressure = (ValidTileCount > 0)
            ? (TotalCommercePressure / static_cast<float>(ValidTileCount))
            : 0.0f;

        const float EconomicOpenness = Civ.CulturalDNA.EconomicOpenness;
        const float TheologicalRigidity = Civ.CulturalDNA.TheologicalRigidity;
        const float Militarism = Civ.CulturalDNA.Militarism;
        const FString GovernmentString = Civ.GovernmentType.ToString();

        TArray<FGameplayTag> Candidates;

        // ADDED: Context-sensitive archetype ordering.
        // These are safe-queried; only registered tags will be considered.
        if (GovernmentString.Contains(TEXT("Theocracy")) || TheologicalRigidity >= 65.0f)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Tithe"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Scrip"));
        }

        if (GovernmentString.Contains(TEXT("Authoritarian")) || GovernmentString.Contains(TEXT("Militar")) || Militarism >= 65.0f)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.StateIssue"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.IronMark"));
        }

        if (AverageCommercePressure >= 0.55f || EconomicOpenness >= 65.0f)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.TradeCoin"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.MintedCoin"));
        }

        if (bHasCoastalTile)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Shell"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.TradeCoin"));
        }

        if (bHasMountainTile)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.IronMark"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.MintedCoin"));
        }

        if (bHasForestTile)
        {
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.TallyStick"));
            AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Scrip"));
        }

        // ADDED: Always provide generic fallbacks if those tags exist in the project.
        AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Standard"));
        AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Coin"));
        AddCandidateIfValid(Candidates, TEXT("WorldForge.Currency.Barter"));

        if (Candidates.Num() == 0 || RNG == nullptr)
        {
            return FGameplayTag();
        }

        const int32 ChosenIndex = RNG->NextInt(UWorldForgeRNG::BRANCH_CIV_START, 0, Candidates.Num() - 1);
        return Candidates[ChosenIndex];
    }
}

void FCivEconomySeedPass::Execute(
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG
)
{
    // MODIFIED: Real economy seeding now attempts to assign a currency tag per civ
    // using live government/culture/world context, while remaining safe if tags
    // have not yet been registered in project settings.

    if (RNG == nullptr)
    {
        return;
    }

    for (FCivilizationData& Civ : Civs)
    {
        // KEPT EXISTING: Do not overwrite an already valid currency tag.
        if (Civ.CurrencyTag.IsValid())
        {
            continue;
        }

        const FGameplayTag ChosenCurrencyTag = ChooseCurrencyTagForCiv(Civ, WorldTiles, RNG);
        if (ChosenCurrencyTag.IsValid())
        {
            // ADDED: Final assignment only occurs when a registered tag was found.
            Civ.CurrencyTag = ChosenCurrencyTag;
        }
    }
}
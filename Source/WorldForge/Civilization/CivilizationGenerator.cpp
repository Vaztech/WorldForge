#include "CivilizationGenerator.h"
#include "../Core/WorldForgeRNG.h"
#include "Generation/CivGenContext.h"         // ADDED
#include "Generation/CivPlacementPass.h"       // ADDED
#include "Generation/CivCultureSeedPass.h"     // ADDED
#include "Generation/CivGovernmentSeedPass.h"  // ADDED
#include "Generation/CivEconomySeedPass.h"     // ADDED
#include "Generation/CivDiplomacySeedPass.h"   // ADDED

TArray<FCivilizationData> UCivilizationGenerator::GenerateCivilizations(
    int32 StartingCivCount, const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth, UWorldForgeRNG* RNG)
{
    // ADDED: Build generation context
    FCivGenContext Context;
    Context.StartingCivCount = StartingCivCount;
    Context.WorldTiles = &WorldTiles;
    Context.GridWidth = GridWidth;
    Context.RNG = RNG;

    // KEPT EXISTING: Placement/create pass owns starting civ creation
    FCivPlacementPass::Execute(*this, Context);

    // ADDED: Cultural DNA must be seeded before downstream government/economy/diplomacy passes.
    FCivCultureSeedPass::Execute(*this, Context.GeneratedCivs, WorldTiles, RNG);

    // MODIFIED: Government seeding pass now performs real government assignment.
    FCivGovernmentSeedPass::Execute(Context.GeneratedCivs, WorldTiles, RNG);

    // MODIFIED: Economy seeding pass now performs real currency assignment.
    FCivEconomySeedPass::Execute(Context.GeneratedCivs, WorldTiles, RNG);

    // MODIFIED: Diplomacy seeding pass now performs real relationship seeding.
    FCivDiplomacySeedPass::Execute(Context.GeneratedCivs, WorldTiles, RNG);

    UE_LOG(LogTemp, Log, TEXT("[CivGen] Generated %d civilizations"), Context.GeneratedCivs.Num());
    return Context.GeneratedCivs;
}

void UCivilizationGenerator::GenerateCulturalDNA(
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG)
{
    // KEPT EXISTING: Public entry point remains available and routes through the dedicated culture pass.
    FCivCultureSeedPass::Execute(*this, Civs, WorldTiles, RNG);
}

int32 UCivilizationGenerator::FindStartingTile(
    const TArray<FWorldTileData>& Tiles,
    const TArray<int32>& Used, UWorldForgeRNG* RNG) const
{
    if (Tiles.Num() == 0) return -1;

    for (int32 Attempt = 0; Attempt < 30; Attempt++)
    {
        const int32 Idx = RNG->NextInt(UWorldForgeRNG::BRANCH_CIV_START, 0, Tiles.Num() - 1);

        if (Used.Contains(Idx)) continue;
        if (!Tiles[Idx].BiomeType.IsValid()) continue;
        if (Tiles[Idx].Elevation < 0.1f) continue;

        return Idx;
    }

    for (int32 i = 0; i < Tiles.Num(); i++)
    {
        if (!Used.Contains(i) && Tiles[i].Elevation >= 0.1f)
            return i;
    }

    return 0;
}

FGameplayTag UCivilizationGenerator::AssignRaceFromBiome(
    FGameplayTag BiomeType, UWorldForgeRNG* RNG) const
{
    const FString BS = BiomeType.ToString();

    if (BS.Contains(TEXT("Mountain")))
    {
        return RNG->NextFloat(UWorldForgeRNG::BRANCH_CIV_START) < 0.7f
            ? FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")))
            : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni")));
    }

    if (BS.Contains(TEXT("Forest")))
    {
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_CIV_START);

        if (R < 0.4f) return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf")));
        if (R < 0.7f) return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin")));

        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.DarkElf")));
    }

    if (BS.Contains(TEXT("Coastal")))
    {
        return RNG->NextFloat(UWorldForgeRNG::BRANCH_CIV_START) < 0.6f
            ? FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller")))
            : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Namekian")));
    }

    const TArray<FGameplayTag>& All = GetAllRaceTags();
    return All[RNG->NextInt(UWorldForgeRNG::BRANCH_CIV_START, 0, All.Num() - 1)];
}

float UCivilizationGenerator::GenerateDNAAxis(float Low, float High, UWorldForgeRNG* RNG) const
{
    return FMath::Lerp(Low, High, RNG->NextFloat(UWorldForgeRNG::BRANCH_CULTURAL_DNA));
}

const TArray<FGameplayTag>& UCivilizationGenerator::GetAllRaceTags()
{
    static TArray<FGameplayTag> Tags;

    if (Tags.Num() == 0)
    {
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.DarkElf"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Namekian"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Construct"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin"))));
        Tags.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))));
    }

    return Tags;
}
#include "CivilizationGenerator.h"
#include "../Core/WorldForgeRNG.h"

TArray<FCivilizationData> UCivilizationGenerator::GenerateCivilizations(
    int32 StartingCivCount, const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth, UWorldForgeRNG* RNG)
{
    TArray<FCivilizationData> Result;
    TArray<int32> UsedTileIDs;

    for (int32 i = 0; i < StartingCivCount; i++)
    {
        FCivilizationData Civ;
        Civ.CivID = i;
        Civ.Name = FString::Printf(TEXT("Civilization_%d"), i);

        const int32 StartTile = FindStartingTile(WorldTiles, UsedTileIDs, RNG);
        if (StartTile >= 0 && WorldTiles.IsValidIndex(StartTile))
        {
            Civ.ControlledTileIDs.Add(StartTile);
            UsedTileIDs.Add(StartTile);

            Civ.RaceType = AssignRaceFromBiome(WorldTiles[StartTile].BiomeType, RNG);
        }
        else
        {
            Civ.RaceType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")));
        }

        Result.Add(Civ);
    }

    UE_LOG(LogTemp, Log, TEXT("[CivGen] Generated %d civilizations"), Result.Num());
    return Result;
}

void UCivilizationGenerator::GenerateCulturalDNA(
    TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& WorldTiles,
    UWorldForgeRNG* RNG)
{
    for (FCivilizationData& Civ : Civs)
    {
        FCultureData& DNA = Civ.CulturalDNA;

        DNA.CivID = Civ.CivID;
        DNA.HierarchyTolerance = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.OutgroupHostility = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.LaborEthics = GenerateDNAAxis(20.f, 80.f, RNG);
        DNA.Militarism = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.MagicReverence = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.Individualism = GenerateDNAAxis(20.f, 80.f, RNG);
        DNA.TheologicalRigidity = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.EconomicOpenness = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.ExpansionDrive = GenerateDNAAxis(10.f, 90.f, RNG);
        DNA.CosmicDevotion = GenerateDNAAxis(10.f, 90.f, RNG);

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
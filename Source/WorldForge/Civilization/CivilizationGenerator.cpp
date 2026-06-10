#include "CivilizationGenerator.h"
#include "../Core/WorldForgeRNG.h"
#include "Generation/CivGenContext.h"         // ADDED
#include "Generation/CivPlacementPass.h"       // ADDED
#include "Generation/CivCultureSeedPass.h"     // ADDED
#include "Generation/CivGovernmentSeedPass.h"  // ADDED
#include "Generation/CivEconomySeedPass.h"     // ADDED
#include "Generation/CivDiplomacySeedPass.h"   // ADDED
#include "../World/WorldPlacementValidation.h"

namespace
{
    constexpr int32 MaxLoggedStartTileRejections = 8;

    struct FCivPlacementDiagnostics
    {
        int32 RejectedWater = 0;
        int32 RejectedInvalid = 0;
        int32 LoggedSamples = 0;
    };

    FCivPlacementDiagnostics& GetCivPlacementDiagnostics()
    {
        static FCivPlacementDiagnostics Diagnostics;
        return Diagnostics;
    }

    void ResetCivPlacementDiagnostics()
    {
        GetCivPlacementDiagnostics() = FCivPlacementDiagnostics();
    }

    void RecordRejectedStartTile(
        int32 TileIndex,
        int32 GridWidth,
        const FWorldTileData& Tile,
        EWorldPlacementRejectReason Reason)
    {
        FCivPlacementDiagnostics& Diagnostics = GetCivPlacementDiagnostics();

        if (Reason == EWorldPlacementRejectReason::Water)
        {
            ++Diagnostics.RejectedWater;
        }
        else
        {
            ++Diagnostics.RejectedInvalid;
        }

        if (Diagnostics.LoggedSamples >= MaxLoggedStartTileRejections || GridWidth <= 0)
        {
            return;
        }

        ++Diagnostics.LoggedSamples;

        const FString BiomeName = Tile.BiomeType.IsValid() ? Tile.BiomeType.ToString() : TEXT("<invalid>");
        const int32 X = TileIndex % GridWidth;
        const int32 Y = TileIndex / GridWidth;

        UE_LOG(LogTemp, Log,
            TEXT("[CivGen] Rejected start tile. Tile=(%d,%d) Reason=%s Biome=%s Elev=%.3f"),
            X,
            Y,
            WorldForgePlacement::ToReasonString(Reason),
            *BiomeName,
            Tile.Elevation);
    }

    void LogStartPlacementFilters(
        int32 Candidates,
        int32 Accepted,
        int32 RejectedWater,
        int32 RejectedLava,
        int32 RejectedBiome)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CivGen] Start placement filters. Candidates=%d Accepted=%d RejectedWater=%d RejectedLava=%d RejectedBiome=%d"),
            Candidates,
            Accepted,
            RejectedWater,
            RejectedLava,
            RejectedBiome);
    }
}

TArray<FCivilizationData> UCivilizationGenerator::GenerateCivilizations(
    int32 StartingCivCount, const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth, UWorldForgeRNG* RNG)
{
    ResetCivPlacementDiagnostics();

    // ADDED: Build generation context
    FCivGenContext Context;
    Context.StartingCivCount = StartingCivCount;
    Context.WorldTiles = &WorldTiles;
    Context.GridWidth = GridWidth;
    Context.RNG = RNG;

    // KEPT EXISTING: Placement/create pass owns starting civ creation
    FCivPlacementPass::Execute(*this, Context);

    const FCivPlacementDiagnostics& PlacementDiagnostics = GetCivPlacementDiagnostics();
    UE_LOG(LogTemp, Log,
        TEXT("[CivGen] Placement summary: Civs=%d RejectedWater=%d RejectedInvalid=%d"),
        Context.GeneratedCivs.Num(),
        PlacementDiagnostics.RejectedWater,
        PlacementDiagnostics.RejectedInvalid);

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

// ═══════════════════════════════════════════════════════════════════
// Territory ownership — BFS flood-fill from each civ's start tile
// ═══════════════════════════════════════════════════════════════════

void UCivilizationGenerator::ClaimTerritories(
    TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    if (Civs.Num() == 0 || WorldTiles.Num() == 0 || GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    // Clear previous ownership before re-claiming.
    for (FWorldTileData& Tile : WorldTiles)
    {
        Tile.ControllingCivID = -1;
    }

    for (FCivilizationData& Civ : Civs)
    {
        Civ.ControlledTileIDs.Reset();
    }

    // Minimum elevation to count as claimable land.
    // Matches FindStartingTile's rejection threshold.
    static constexpr float LandElevationMin = 0.1f;

    // Territory radius range: base 5 tiles, up to 8 with max ExpansionDrive.
    static constexpr int32 BaseRadius = 5;
    static constexpr int32 MaxExtraRadius = 3;

    // 4-directional BFS neighbors (N, S, E, W).
    static constexpr int32 DX[] = { 0,  0, 1, -1 };
    static constexpr int32 DY[] = { -1, 1, 0,  0 };

    int32 TotalClaimed = 0;

    for (FCivilizationData& Civ : Civs)
    {
        if (Civ.bCollapsed)
        {
            continue;
        }

        const int32 Origin = Civ.StartTileIndex;
        if (!WorldTiles.IsValidIndex(Origin))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[CivGen] Civ %d (%s) has no valid StartTileIndex (%d). Skipping territory claim."),
                Civ.CivID, *Civ.Name, Origin);
            continue;
        }

        const float Drive = FMath::Clamp(Civ.CulturalDNA.ExpansionDrive, 0.f, 100.f);
        const int32 Radius = BaseRadius + FMath::RoundToInt((Drive / 100.f) * MaxExtraRadius);

        const int32 OriginX = Origin % GridWidth;
        const int32 OriginY = Origin / GridWidth;

        TArray<int32> Queue;
        TSet<int32> Visited;
        TMap<int32, int32> Distance;

        Queue.Add(Origin);
        Visited.Add(Origin);
        Distance.Add(Origin, 0);

        int32 Head = 0;

        while (Head < Queue.Num())
        {
            const int32 Current = Queue[Head++];
            const int32 CurDist = Distance[Current];

            FWorldTileData& Tile = WorldTiles[Current];

            // Only claim unclaimed land tiles.
            if (Tile.Elevation >= LandElevationMin && Tile.ControllingCivID < 0)
            {
                Tile.ControllingCivID = Civ.CivID;
                Civ.ControlledTileIDs.AddUnique(Current);
                ++TotalClaimed;
            }

            if (CurDist >= Radius)
            {
                continue;
            }

            const int32 CurX = Current % GridWidth;
            const int32 CurY = Current / GridWidth;

            for (int32 D = 0; D < 4; ++D)
            {
                const int32 NX = CurX + DX[D];
                const int32 NY = CurY + DY[D];

                if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight)
                {
                    continue;
                }

                const int32 NIdx = NY * GridWidth + NX;

                if (Visited.Contains(NIdx))
                {
                    continue;
                }

                if (WorldTiles[NIdx].Elevation < LandElevationMin)
                {
                    continue;
                }

                if (WorldTiles[NIdx].ControllingCivID >= 0)
                {
                    continue;
                }

                Visited.Add(NIdx);
                Distance.Add(NIdx, CurDist + 1);
                Queue.Add(NIdx);
            }
        }

        UE_LOG(LogTemp, Log,
            TEXT("[CivGen] Civ %d (%s) claimed %d tiles. Radius=%d ExpansionDrive=%.0f Origin=(%d,%d)"),
            Civ.CivID, *Civ.Name, Civ.ControlledTileIDs.Num(), Radius, Drive, OriginX, OriginY);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CivGen] ClaimTerritories complete. %d total tiles claimed across %d civs."),
        TotalClaimed, Civs.Num());
}

int32 UCivilizationGenerator::FindStartingTile(
    const TArray<FWorldTileData>& Tiles,
    const TArray<int32>& Used,
    int32 GridWidth,
    UWorldForgeRNG* RNG) const
{
    if (Tiles.Num() == 0 || GridWidth <= 0 || RNG == nullptr)
    {
        return INDEX_NONE;
    }

    int32 CandidateCount = 0;
    int32 AcceptedCount = 0;
    int32 RejectedWater = 0;
    int32 RejectedLava = 0;
    int32 RejectedBiome = 0;

    auto RecordLocalRejection = [&](int32 TileIndex, const FWorldTileData& Tile, EWorldPlacementRejectReason Reason)
    {
        ++CandidateCount;

        switch (Reason)
        {
        case EWorldPlacementRejectReason::Water:
            ++RejectedWater;
            break;
        case EWorldPlacementRejectReason::Lava:
            ++RejectedLava;
            break;
        default:
            ++RejectedBiome;
            break;
        }

        RecordRejectedStartTile(TileIndex, GridWidth, Tile, Reason);
    };

    auto AcceptTile = [&](int32 TileIndex) -> int32
    {
        ++CandidateCount;
        ++AcceptedCount;
        LogStartPlacementFilters(CandidateCount, AcceptedCount, RejectedWater, RejectedLava, RejectedBiome);
        return TileIndex;
    };

    for (int32 Attempt = 0; Attempt < 30; Attempt++)
    {
        const int32 Idx = RNG->NextInt(UWorldForgeRNG::BRANCH_CIV_START, 0, Tiles.Num() - 1);

        if (Used.Contains(Idx))
        {
            continue;
        }

        const FWorldTileData& Candidate = Tiles[Idx];
        const EWorldPlacementRejectReason Reason = WorldForgePlacement::ClassifyMajorSettlementTile(Candidate);

        if (Reason != EWorldPlacementRejectReason::None)
        {
            RecordLocalRejection(Idx, Candidate, Reason);
            continue;
        }

        return AcceptTile(Idx);
    }

    for (int32 i = 0; i < Tiles.Num(); i++)
    {
        if (Used.Contains(i))
        {
            continue;
        }

        const FWorldTileData& Candidate = Tiles[i];
        const EWorldPlacementRejectReason Reason = WorldForgePlacement::ClassifyMajorSettlementTile(Candidate);

        if (Reason != EWorldPlacementRejectReason::None)
        {
            RecordLocalRejection(i, Candidate, Reason);
            continue;
        }

        return AcceptTile(i);
    }

    LogStartPlacementFilters(CandidateCount, AcceptedCount, RejectedWater, RejectedLava, RejectedBiome);
    UE_LOG(LogTemp, Warning, TEXT("[CivGen] WARNING: No valid start tile found after placement filters."));
    return INDEX_NONE;
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

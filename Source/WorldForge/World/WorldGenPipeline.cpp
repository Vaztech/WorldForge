#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "GameplayTagContainer.h"
#include "../Civilization/CivilizationGenerator.h"
#include "../Civilization/HistoricalSimulation.h"
#include "../Civilization/WorldHistory/WorldHistoryGenerator.h"
#include "../Civilization/WorldHistory/WorldHistoryContext.h"
#include "../Economy/WorldEconomySimulation.h"
#include "VoxelWorldManager.h"

namespace
{
    static FString CleanNameArtifacts(const FString& Input)
    {
        FString Result = Input;

        // Remove triple repeated characters (rrr -> rr)
        for (int32 i = 0; i < Result.Len() - 2; ++i)
        {
            if (Result[i] == Result[i + 1] && Result[i] == Result[i + 2])
            {
                Result.RemoveAt(i + 2);
                --i;
            }
        }

        // Remove repeated 2-letter syllables (e.g. riri -> ri)
        for (int32 i = 0; i < Result.Len() - 3; ++i)
        {
            if (Result[i] == Result[i + 2] &&
                Result[i + 1] == Result[i + 3])
            {
                Result.RemoveAt(i + 2, 2);
                --i;
            }
        }

        // Keep previous replacements (safe)
        Result.ReplaceInline(TEXT("ririr"), TEXT("rir"));
        Result.ReplaceInline(TEXT("lalal"), TEXT("lal"));

        return Result;
    }
}

void UWorldGenPipeline::GenerateWorld(
    int64 InWorldSeed,
    FWorldGenConfig Config,
    UWorldForgeRNG* RNG)
{
    bComplete = false;
    CurrentConfig = Config;
    WorldSeed = InWorldSeed;
    RNGRef = RNG;
    WorldRef = GetOuter() ? GetOuter()->GetWorld() : nullptr;
    GridWidth = GetDimension(Config.WorldSize);
    GridHeight = GridWidth;

    if (!RNGRef)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGen] GenerateWorld: RNG is null! Aborting."));
        return;
    }

    // Reset generated results for repeat-safe execution
    Civilizations.Reset();
    HistoryLog.Reset();
    Currencies.Reset();
    WorldTraits.Reset();

    // Allocate tiles
    WorldTiles.SetNum(GridWidth * GridHeight);
    for (int32 i = 0; i < WorldTiles.Num(); i++)
    {
        WorldTiles[i].TileID = i;
        WorldTiles[i].KnowledgeState.TileID = i;
        WorldTiles[i].KnowledgeState.KnowledgeLevel = EKnowledgeLevel::Unexplored;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Start Seed=%lld Grid=%dx%d"),
        WorldSeed, GridWidth, GridHeight);

    Step01_InitializeSeed();
    Step02_GenerateHeightmap();
    Step03_RunTectonics();
    Step04_AssignBiomes();
    Step05_InitVoxelChunks();
    Step06_PCGDecorationPass();
    Step07_ApplyWorldDriver();
    Step08_SeedResources();
    Step09_PlaceCivilizations();
    Step10_GenerateCulturalDNA();
    Step11_GenerateEconomyLayer();
    Step12_GenerateCosmicRoster();
    Step13_RunPreHistory();
    Step14_AssignWorldTraits();
    Step15_RecordFinalState();
    Step16_FinalizeWorldMap();

    bComplete = true;
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Complete Tiles=%d Civs=%d Events=%d Currencies=%d"),
        WorldTiles.Num(), Civilizations.Num(),
        HistoryLog.Num(), Currencies.Num());
}

int32 UWorldGenPipeline::GetDimension(EWorldSize S) const
{
    switch (S)
    {
    case EWorldSize::Small:  return 128;
    case EWorldSize::Large:  return 512;
    case EWorldSize::OMG:    return 1024;
    default:                 return 256;
    }
}

void UWorldGenPipeline::Step01_InitializeSeed()
{
    RNGRef->InitializeWithSeed(WorldSeed);
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step01 done"));
}

void UWorldGenPipeline::Step02_GenerateHeightmap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02: Heightmap..."));

    for (int32 Y = 0; Y < GridHeight; Y++)
    {
        for (int32 X = 0; X < GridWidth; X++)
        {
            FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
            Tile.Elevation = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN);
            Tile.Temperature = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_BIOME);
            Tile.Rainfall = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_BIOME);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02 done"));
}

void UWorldGenPipeline::Step03_RunTectonics()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03: Tectonics -- TODO"));
}

void UWorldGenPipeline::Step04_AssignBiomes()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step04: Biomes..."));

    for (FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.Elevation > 0.80f)
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")));
        }
        else if (Tile.Elevation > 0.60f && Tile.Rainfall > 0.5f)
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")));
        }
        else if (Tile.Elevation > 0.60f)
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")));
        }
        else if (Tile.Elevation > 0.35f && Tile.Temperature > 0.7f)
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")));
        }
        else if (Tile.Elevation > 0.35f)
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")));
        }
        else
        {
            Tile.BiomeType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")));
        }

        // Voxel profile -- bSmoothSDF ALWAYS true
        Tile.VoxelProfile.bSmoothSDF = true;
        Tile.VoxelProfile.NoiseAmplitude = 50.0f + Tile.Elevation * 100.0f;
        Tile.VoxelProfile.NoiseFrequency = 0.02f;
        Tile.VoxelProfile.OctaveCount = 4;
        Tile.VoxelProfile.bEnableCaves = (Tile.Elevation > 0.4f);
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step04 done"));
}

void UWorldGenPipeline::Step05_InitVoxelChunks()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step05: Init voxel chunks..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: WorldRef is null."));
        return;
    }

    UVoxelWorldManager* VoxelMgr = WorldRef->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelMgr)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: UVoxelWorldManager not found."));
        return;
    }

    const bool bAccepted = VoxelMgr->GenerateTerrainFromTiles(
        WorldTiles,
        GridWidth,
        GridHeight,
        CurrentConfig.TerrainMode,
        CurrentConfig.BlockHeightStep);

    if (!bAccepted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05: terrain request was not accepted."));
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WorldGen] Step05 done. TerrainMode=%s BlockHeightStep=%d"),
        CurrentConfig.TerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        CurrentConfig.BlockHeightStep
    );
}

// Steps 06-08 remain stubs for now
void UWorldGenPipeline::Step06_PCGDecorationPass()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06 -- TODO"));
}

void UWorldGenPipeline::Step07_ApplyWorldDriver()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step07 -- TODO"));
}

void UWorldGenPipeline::Step08_SeedResources()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step08 -- TODO"));
}

// Steps 12, 14, 15, 16 remain stubs for now
void UWorldGenPipeline::Step12_GenerateCosmicRoster()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step12 -- TODO"));
}

void UWorldGenPipeline::Step14_AssignWorldTraits()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step14 -- TODO"));
}

void UWorldGenPipeline::Step15_RecordFinalState()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step15 -- TODO"));
}

void UWorldGenPipeline::Step16_FinalizeWorldMap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step16 -- TODO"));
}

void UWorldGenPipeline::Step09_PlaceCivilizations()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: Placing civilizations..."));

    if (!RNGRef)
    {
        return;
    }

    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);
    Civilizations = CivGen->GenerateCivilizations(
        CurrentConfig.StartingCivCount, WorldTiles, GridWidth, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: %d civs placed"), Civilizations.Num());
}

void UWorldGenPipeline::Step10_GenerateCulturalDNA()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA..."));

    if (!RNGRef)
    {
        return;
    }

    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);
    CivGen->GenerateCulturalDNA(Civilizations, WorldTiles, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA done for %d civs"), Civilizations.Num());
}

void UWorldGenPipeline::Step11_GenerateEconomyLayer()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step11: Economy layer..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: WorldRef null"));
        return;
    }

    if (UWorldEconomySimulation* Econ = WorldRef->GetSubsystem<UWorldEconomySimulation>())
    {
        Econ->GenerateWorldEconomy(Civilizations, WorldTiles, Currencies, RNGRef);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: UWorldEconomySimulation not found"));
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step11: %d currencies"), Currencies.Num());
}

void UWorldGenPipeline::Step13_RunPreHistory()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step13: Pre-history..."));

    if (!RNGRef)
    {
        return;
    }

    FHistoricalSimConfig SimCfg;
    SimCfg.TotalYears = CurrentConfig.PreHistoryYears;
    SimCfg.WarFrequency = CurrentConfig.WarFrequency;
    SimCfg.DisasterFrequency = CurrentConfig.DisasterFrequency;
    SimCfg.EconomicComplexity = CurrentConfig.EconomicComplexity;

    UHistoricalSimulation* HistSim = NewObject<UHistoricalSimulation>(this);
    HistSim->RunSimulation(SimCfg, Civilizations, WorldTiles, Currencies, HistoryLog, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step13: %d events"), HistoryLog.Num());

    {
        FWorldHistoryContext Context;
        Context.RNG = RNGRef;
        Context.CurrentYear = CurrentConfig.PreHistoryYears;

        FWorldHistoryGenerator Generator;
        Generator.Run(Context);

        const int32 NameCount = FMath::Min(Civilizations.Num(), Context.GeneratedNames.Num());

        TSet<FString> UsedNames;

        for (int32 Index = 0; Index < NameCount; ++Index)
        {
            FString RawName = Context.GeneratedNames[Index];

            RawName = CleanNameArtifacts(RawName);

            if (RawName.Len() > 0)
            {
                RawName[0] = FChar::ToUpper(RawName[0]);
            }

            FString FinalName = RawName;

            int32 Suffix = 1;
            while (UsedNames.Contains(FinalName))
            {
                FinalName = FString::Printf(TEXT("%s %d"), *RawName, Suffix);
                ++Suffix;
            }

            UsedNames.Add(FinalName);

            FText NameText = FText::FromString(FinalName);
            Civilizations[Index].Name = NameText.ToString();

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[WorldHistory] Assigned Civilization %d Name: %s"),
                Civilizations[Index].CivID,
                *Civilizations[Index].Name);
        }

        if (Context.GeneratedFigures.Num() == Civilizations.Num())
        {
            for (int32 Index = 0; Index < Civilizations.Num(); ++Index)
            {
                Civilizations[Index].LegendaryFigure = Context.GeneratedFigures[Index];

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[WorldHistory] Civilization %d Figure Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].LegendaryFigure);
            }
        }

        for (int32 Index = 0; Index < NameCount; ++Index)
        {
            if (Context.RaceOrigins.IsValidIndex(Index))
            {
                Civilizations[Index].RaceOrigin = Context.RaceOrigins[Index];

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[WorldHistory] Civilization %d Origin Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].RaceOrigin);
            }
        }
    }
}
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "CivilizationGenerator.generated.h"

class UWorldForgeRNG;
class FCivPlacementPass;      // ADDED
class FCivCultureSeedPass;    // ADDED
class FCivRaceSeedPass;       // ADDED
class FCivNamingPass;         // ADDED
class FCivGovernmentSeedPass; // ADDED
class FCivEconomySeedPass;    // ADDED
class FCivDiplomacySeedPass;  // ADDED

UCLASS()
class WORLDFORGE_API UCivilizationGenerator : public UObject
{
    GENERATED_BODY()

public:
    TArray<FCivilizationData> GenerateCivilizations(
        int32                         StartingCivCount,
        const TArray<FWorldTileData>& WorldTiles,
        int32                         GridWidth,
        UWorldForgeRNG* RNG
    );

    void GenerateCulturalDNA(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& WorldTiles,
        UWorldForgeRNG* RNG
    );

    // ── Territory ownership ──────────────────────────────────────────
    // BFS flood-fill from each civ's StartTileIndex, claiming land tiles
    // within a radius scaled by CulturalDNA.ExpansionDrive.
    // Sets FWorldTileData::ControllingCivID and populates
    // FCivilizationData::ControlledTileIDs.
    //
    // Call AFTER GenerateCivilizations() returns, from the pipeline,
    // where the tile array is mutable.
    static void ClaimTerritories(
        TArray<FCivilizationData>& Civs,
        TArray<FWorldTileData>& WorldTiles,
        int32                      GridWidth,
        int32                      GridHeight
    );

private:
    friend class FCivPlacementPass;      // ADDED
    friend class FCivCultureSeedPass;    // ADDED
    friend class FCivRaceSeedPass;       // ADDED
    friend class FCivNamingPass;         // ADDED
    friend class FCivGovernmentSeedPass; // ADDED
    friend class FCivEconomySeedPass;    // ADDED
    friend class FCivDiplomacySeedPass;  // ADDED

    int32        FindStartingTile(const TArray<FWorldTileData>& Tiles, const TArray<int32>& Used, int32 GridWidth, UWorldForgeRNG* RNG) const;
    FGameplayTag AssignRaceFromBiome(FGameplayTag BiomeType, UWorldForgeRNG* RNG) const;
    float        GenerateDNAAxis(float Low, float High, UWorldForgeRNG* RNG) const;
    static const TArray<FGameplayTag>& GetAllRaceTags();
};

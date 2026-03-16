#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "CivilizationGenerator.generated.h"

class UWorldForgeRNG;

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

private:
    int32        FindStartingTile(const TArray<FWorldTileData>& Tiles, const TArray<int32>& Used, UWorldForgeRNG* RNG) const;
    FGameplayTag AssignRaceFromBiome(FGameplayTag BiomeType, UWorldForgeRNG* RNG) const;
    float        GenerateDNAAxis(float Low, float High, UWorldForgeRNG* RNG) const;
    static const TArray<FGameplayTag>& GetAllRaceTags();
};
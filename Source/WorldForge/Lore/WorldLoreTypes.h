// WorldLoreTypes.h — WorldForge Lore Infrastructure
// All lore data structs. FWorldLoreState is IMMUTABLE after generation.
// Every field tagged SaveGame per project invariant.
//
// CROSS-REFERENCE CONVENTION:
//   Lore-internal IDs (ArtifactID, CityID, LegendID, ColonistID, etc.) → FName
//   Cross-refs to FCivilizationData/FHistoricalFigure                  → int32

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldLoreTypes.generated.h"

// ─────────────────────────────────────────────
//  Lineages — dynasties/family traditions
// ─────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreLineage
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LineageID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 CivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<int32> FigureIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DynastyName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText LoreSnippet;
};

// ─────────────────────────────────────────────
//  Battles — resolved named battle records
// ─────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreBattle
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BattleID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 AttackerCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DefenderCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 WinnerCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntPoint TileCoord = FIntPoint::ZeroValue;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Epoch = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText LoreSnippet;
};

// ─────────────────────────────────────────────
//  Artifacts — legendary items forged by civilizations
// ─────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreArtifact
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ArtifactID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText Description;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTag ArtifactType;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ForgedByCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ForgedByFigureID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ForgedInEpoch = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 CurrentHolderCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LostInCityID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTagContainer TraitTags;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float PowerRating = 0.0f;
};

// ─────────────────────────────────────────────
//  Creatures — notable creatures with lairs/lore
// ─────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreCreature
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CreatureID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTag CreatureType;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntPoint LairTile = FIntPoint::ZeroValue;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 AssociatedCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText LoreSnippet;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float ThreatRating = 0.0f;
};

// ─────────────────────────────────────────────
//  Fallen Cities — ruins with history
// ─────────────────────────────────────────────

UENUM(BlueprintType)
enum class ECityFallCause : uint8
{
	War,
	Plague,
	Famine,
	NaturalDisaster,
	CosmicWrath,
	CivilWar,
	Abandonment
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreFallenCity
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CityID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntPoint TileCoord = FIntPoint::ZeroValue;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 OriginalCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ConquerorCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 FoundedInEpoch = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 FellInEpoch = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	ECityFallCause CauseOfFall = ECityFallCause::Abandonment;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText LoreSnippet;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> ArtifactsLostHere;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTag BiomeTag;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float DangerRating = 0.0f;
};

// ─────────────────────────────────────────────
//  Legends — synthesized narratives
// ─────────────────────────────────────────────

UENUM(BlueprintType)
enum class EWorldForgeLoreLegendType : uint8
{
	HeroicBattle,
	TragicFall,
	Creation,
	Discovery,
	Betrayal,
	Prophecy,
	Migration
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FLoreLegend
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LegendID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText Title;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText NarrativeText;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EWorldForgeLoreLegendType LegendType = EWorldForgeLoreLegendType::HeroicBattle;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTag CosmicDomain;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<int32> ReferencedCivIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> ReferencedArtifactIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<int32> ReferencedFigureIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> ReferencedCityIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Epoch = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float Renown = 0.0f;
};

// ─────────────────────────────────────────────
//  Colonist Backstories — starting colonist lore
// ─────────────────────────────────────────────

UENUM(BlueprintType)
enum class EBackstoryArchetype : uint8
{
	Exile,
	Veteran,
	Scholar,
	Orphan,
	Merchant,
	Criminal,
	Pilgrim,
	Artisan
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FColonistBackstory
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ColonistID;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 HomeCivID = -1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EBackstoryArchetype Archetype = EBackstoryArchetype::Exile;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FText BackstoryText;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTagContainer PersonalityTraits;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> KnownArtifactIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> KnownLegendIDs;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float CombatAptitude = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float CraftingAptitude = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float SocialAptitude = 0.0f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float ScholarAptitude = 0.0f;
};

// ─────────────────────────────────────────────
//  FWorldLoreState — top-level immutable container
// ─────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldLoreState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreLineage> Lineages;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreBattle> Battles;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreArtifact> Artifacts;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreCreature> Creatures;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreFallenCity> FallenCities;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FLoreLegend> Legends;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FColonistBackstory> ColonistBackstories;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LoreSeed = 0;

	const FLoreLineage* FindLineage(FName ID) const;
	const FLoreBattle* FindBattle(FName ID) const;
	const FLoreArtifact* FindArtifact(FName ID) const;
	const FLoreCreature* FindCreature(FName ID) const;
	const FLoreFallenCity* FindFallenCity(FName ID) const;
	const FLoreLegend* FindLegend(FName ID) const;
	const FColonistBackstory* FindColonist(FName ID) const;

	TArray<const FLoreArtifact*> GetArtifactsByCiv(int32 CivID) const;
	TArray<const FLoreFallenCity*> GetFallenCitiesByCiv(int32 CivID) const;
	TArray<const FLoreLegend*> GetLegendsByEpoch(int32 Epoch) const;

	int32 NumLineages() const { return Lineages.Num(); }
	int32 NumBattles() const { return Battles.Num(); }
	int32 NumArtifacts() const { return Artifacts.Num(); }
	int32 NumCreatures() const { return Creatures.Num(); }
	int32 NumFallenCities() const { return FallenCities.Num(); }
	int32 NumLegends() const { return Legends.Num(); }
	int32 NumColonists() const { return ColonistBackstories.Num(); }

	FString GetSummaryString() const;

	/** Debug: writes the full generated lore contents to Output Log */
	void LogFullLore() const;
};
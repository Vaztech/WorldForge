#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WorldForgeTraitTypes.generated.h"

// ============================================================================
// WorldForgeTraitTypes.h
//
// Central trait data definitions.
//
// Future us:
// - Add new trait tuning fields here when traits need to affect more systems.
// - Keep this file data-only.
// - Put behavior/math in WorldForgeTraitLibrary, processors, or future systems.
// ============================================================================

UENUM(BlueprintType)
enum class EWorldForgeTraitPolarity : uint8
{
	Positive UMETA(DisplayName = "Positive"),
	Negative UMETA(DisplayName = "Negative"),
	Neutral  UMETA(DisplayName = "Neutral"),
	Strange  UMETA(DisplayName = "Strange")
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeTraitDefinition : public FTableRowBase
{
	GENERATED_BODY()

	// Gameplay tag ID for this trait.
	// Example: WorldForge.Trait.Clumsy
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag TraitTag;

	// Player-facing name.
	// Example: Clumsy
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FText DisplayName;

	// Player-facing explanation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FText Description;

	// Positive, negative, neutral, or strange.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EWorldForgeTraitPolarity Polarity = EWorldForgeTraitPolarity::Neutral;

	// Work speed multiplier.
	// 1.0 = normal, 0.8 = 20% slower, 1.15 = 15% faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float WorkEfficiencyMultiplier = 1.0f;

	// Healing multiplier.
	// Fast Healer might be 1.5, Slow Healer might be 0.6.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float HealingMultiplier = 1.0f;

	// Injury chance multiplier.
	// Lucky might be 0.8, Clumsy might be 1.5.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float InjuryChanceMultiplier = 1.0f;

	// Disease / infection resistance multiplier.
	// Higher = better resistance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float ImmunityMultiplier = 1.0f;

	// Mood modifier applied by processors later.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float MoodModifier = 0.0f;

	// Social modifier applied by social/job scoring later.
	// Positive = more socially capable, negative = more socially withdrawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float SocialModifier = 0.0f;

	// How naturally likeable this trait makes the colonist.
	// Positive values make others prefer them in social/job scoring.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float LikeabilityModifier = 0.0f;

	// How annoying this trait makes the colonist to others.
	// Higher values increase irritation, social friction, rivalry, or avoidance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float AnnoyanceModifier = 0.0f;

	// Optional skill modifiers.
	// Key: Skill tag, Value: additive skill modifier.
	// Example: WorldForge.Skill.Crafting -> +10
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TMap<FGameplayTag, float> SkillModifiers;

	// Notes for us/designers. Not intended for player UI.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString DesignerNotes;
};
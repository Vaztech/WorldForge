#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "WorldForgeTraitTypes.h"
#include "WorldForgeTraitLibrary.generated.h"

// ============================================================================
// WorldForgeTraitLibrary.h
//
// Stateless helper library for trait math.
// Future us: processors should call this instead of hardcoding trait effects.
// This keeps HealthProcessor, JobProcessor, ProductionProcessor, etc. cleaner.
// ============================================================================

UCLASS()
class WORLDFORGE_API UWorldForgeTraitLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Finds a trait definition in a DataTable by its TraitTag.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static bool FindTraitDefinition(
		UDataTable* TraitTable,
		FGameplayTag TraitTag,
		FWorldForgeTraitDefinition& OutDefinition);

	// Multiplies all matching WorkEfficiencyMultiplier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateWorkEfficiencyMultiplier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Multiplies all matching HealingMultiplier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateHealingMultiplier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Multiplies all matching InjuryChanceMultiplier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateInjuryChanceMultiplier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Multiplies all matching ImmunityMultiplier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateImmunityMultiplier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Adds all matching MoodModifier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateMoodModifier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Adds all matching SocialModifier values together.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateSocialModifier(
		const FGameplayTagContainer& TraitTags,
		UDataTable* TraitTable);

	// Returns additive skill bonus from traits for one skill tag.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static float CalculateSkillModifier(
		const FGameplayTagContainer& TraitTags,
		FGameplayTag SkillTag,
		UDataTable* TraitTable);
};
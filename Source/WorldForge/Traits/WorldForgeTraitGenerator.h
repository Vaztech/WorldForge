#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "WorldForgeTraitGenerator.generated.h"

// ============================================================================
// WorldForgeTraitGenerator.h
//
// Central random/inherited trait roller.
// Future us:
// - Colonists use this now.
// - NPCs, animals, monsters, and offspring can reuse this later.
// - Keep trait rolling here so processors do not become trait-spaghetti.
// ============================================================================

UCLASS()
class WORLDFORGE_API UWorldForgeTraitGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Rolls starting traits for a newly spawned entity.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static FGameplayTagContainer RollRandomTraitsForNewEntity(
		FGameplayTag RaceTag,
		int32 Seed,
		int32 MinTraits = 1,
		int32 MaxTraits = 3);

	// Rolls child/offspring traits from two parents.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Traits")
	static FGameplayTagContainer RollInheritedTraitsFromParents(
		const FGameplayTagContainer& ParentATraits,
		const FGameplayTagContainer& ParentBTraits,
		int32 Seed,
		float InheritChancePerTrait = 0.45f,
		float MutationChance = 0.05f,
		int32 MaxTraits = 5);
};
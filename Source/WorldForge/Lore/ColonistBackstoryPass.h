// ColonistBackstoryPass.h

#pragma once

#include "CoreMinimal.h"
#include "LoreGenerationContext.h"

/**
 * ColonistBackstoryPass — Phase 3.5 Lore Pass (runs LAST)
 *
 * Generates starting colonists whose backstories reference the world's
 * artifacts, fallen cities, and legends. Each colonist has an archetype,
 * personality traits, skill aptitudes, and knowledge of specific lore.
 *
 * This is the bridge between world history and gameplay — colonists
 * carry the lore into the colony phase as quest hooks and dialogue.
 */
class WORLDFORGE_API FColonistBackstoryPass
{
public:
	/** How many colonists to generate (default 7, typical DF-style starting group) */
	static constexpr int32 DefaultColonistCount = 7;

	static void Execute(FLoreGenerationContext& Ctx, int32 NumColonists = DefaultColonistCount);

private:
	/** Pick archetype weighted by home civ traits */
	static EBackstoryArchetype ChooseArchetype(
		const FCivilizationData& HomeCiv, UWorldForgeRNG& RNG);

	/** Assign personality traits based on archetype */
	static FGameplayTagContainer RollPersonalityTraits(
		EBackstoryArchetype Archetype, UWorldForgeRNG& RNG);

	/** Set skill aptitudes based on archetype */
	static void AssignAptitudes(
		FColonistBackstory& Colonist, EBackstoryArchetype Archetype, UWorldForgeRNG& RNG);

	/** Assign known artifacts and legends (quest hooks) */
	static void AssignKnowledge(
		FColonistBackstory& Colonist,
		const FCivilizationData& HomeCiv,
		FLoreGenerationContext& Ctx);

	/** Generate backstory narrative text */
	static FText GenerateBackstoryText(
		const FColonistBackstory& Colonist,
		const FCivilizationData& HomeCiv,
		const FLoreGenerationContext& Ctx);

	/** Generate a colonist name */
	static FText GenerateColonistName(
		const FCivilizationData& HomeCiv, UWorldForgeRNG& RNG);

	static FName MakeColonistID(int32 Index);
};

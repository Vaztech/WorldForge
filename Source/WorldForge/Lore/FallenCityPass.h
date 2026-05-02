// FallenCityPass.h

#pragma once

#include "CoreMinimal.h"
#include "LoreGenerationContext.h"

/**
 * FallenCityPass — Phase 3.5 Lore Pass
 *
 * Generates ruins from civilizations that lost settlements or collapsed.
 * Each ruin has a cause-of-fall, lore snippet, danger rating, and may
 * inherit artifacts that were lost when the city fell.
 * Feeds directly into the PCG decoration pass (Step06) for ruin mesh placement.
 */
class WORLDFORGE_API FFallenCityPass
{
public:
	static void Execute(FLoreGenerationContext& Ctx);

private:
	/** Decide how many cities fell for a given civ */
	static int32 RollFallenCityCount(const FCivilizationData& Civ, UWorldForgeRNG& RNG);

	/** Pick a cause weighted by world/civ traits */
	static ECityFallCause RollCauseOfFall(
		const FCivilizationData& Civ, bool bHasConqueror, UWorldForgeRNG& RNG);

	/** Pick a tile for the ruin (biased toward civ territory edges) */
	static FIntPoint ChooseRuinTile(
		const FCivilizationData& Civ,
		const FLoreGenerationContext& Ctx);

	/** Generate the lore snippet */
	static FText GenerateLoreSnippet(
		const FLoreFallenCity& City,
		const FCivilizationData& OrigCiv,
		const FCivilizationData* Conqueror);

	/** Generate a ruin name */
	static FText GenerateRuinName(const FCivilizationData& Civ, UWorldForgeRNG& RNG);

	static FName MakeCityID(int32 Index);
};

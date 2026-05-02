// ArtifactForgePass.h

#pragma once

#include "CoreMinimal.h"
#include "LoreGenerationContext.h"

/**
 * ArtifactForgePass — Phase 3.5 Lore Pass
 *
 * For each epoch, rolls artifact creation chances per civ.
 * Warlike civs bias toward weapons, scholarly civs toward tomes, etc.
 * All randomness via UWorldForgeRNG. Output is appended to LoreState.Artifacts.
 */
class WORLDFORGE_API FArtifactForgePass
{
public:
	static void Execute(FLoreGenerationContext& Ctx);

private:
	/** Roll whether a civ forges an artifact this epoch */
	static bool ShouldForgeArtifact(const FCivilizationData& Civ, int32 Epoch, UWorldForgeRNG& RNG);

	/** Pick artifact type based on civ traits */
	static FGameplayTag ChooseArtifactType(const FCivilizationData& Civ, UWorldForgeRNG& RNG);

	/** Assign trait tags (Cursed, Holy, etc.) */
	static FGameplayTagContainer RollTraitTags(UWorldForgeRNG& RNG);

	/** Generate a name from fragment tables */
	static FText GenerateArtifactName(const FGameplayTag& Type, UWorldForgeRNG& RNG);

	/** Generate a one-line description */
	static FText GenerateArtifactDescription(
		const FLoreArtifact& Artifact,
		const FCivilizationData& Civ,
		const FHistoricalFigure* Smith);

	/** Unique ID factory */
	static FName MakeArtifactID(int32 Index);
};

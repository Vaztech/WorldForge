// LegendSynthesisPass.h

#pragma once

#include "CoreMinimal.h"
#include "LoreGenerationContext.h"

/**
 * LegendSynthesisPass — Phase 3.5 Lore Pass
 *
 * Cross-references artifacts, fallen cities, figures, and civ history
 * to synthesize narrative legends. Each legend ties multiple lore elements
 * together into a story with a type, cosmic domain, and renown score.
 *
 * Runs AFTER ArtifactForgePass and FallenCityPass (needs their output).
 */
class WORLDFORGE_API FLegendSynthesisPass
{
public:
	static void Execute(FLoreGenerationContext& Ctx);

private:
	/** Try to synthesize a legend around a specific artifact */
	static bool TrySynthesizeArtifactLegend(
		const FLoreArtifact& Artifact,
		FLoreGenerationContext& Ctx,
		int32& LegendIndex);

	/** Try to synthesize a legend around a fallen city */
	static bool TrySynthesizeFallenCityLegend(
		const FLoreFallenCity& City,
		FLoreGenerationContext& Ctx,
		int32& LegendIndex);

	/** Try to synthesize a legend around a historical figure */
	static bool TrySynthesizeFigureLegend(
		const FHistoricalFigure& Figure,
		FLoreGenerationContext& Ctx,
		int32& LegendIndex);

	/** Pick a cosmic domain for the legend */
	static FGameplayTag ChooseCosmicDomain(
		EWorldForgeLoreLegendType Type,          // ← was ELegendType (collision with WorldHistoryTypes.h)
		UWorldForgeRNG& RNG);

	/** Compute renown based on referenced elements */
	static float ComputeRenown(
		const FLoreLegend& Legend,
		const FLoreGenerationContext& Ctx);

	static FName MakeLegendID(int32 Index);
};
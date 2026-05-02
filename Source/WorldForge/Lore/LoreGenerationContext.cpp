// LoreGenerationContext.cpp — Central lore generation orchestrator

#include "LoreGenerationContext.h"

#include "ArtifactForgePass.h"
#include "FallenCityPass.h"
#include "LegendSynthesisPass.h"
#include "ColonistBackstoryPass.h"

void GenerateWorldLore(FLoreGenerationContext& Context)
{
	Context.LoreState = FWorldLoreState();
	Context.LoreState.LoreSeed = Context.WorldSeed;

	// Existing implemented lore passes.
	// Order matters: later passes reference data created by earlier passes.
	FArtifactForgePass::Execute(Context);
	FFallenCityPass::Execute(Context);
	FLegendSynthesisPass::Execute(Context);
	FColonistBackstoryPass::Execute(Context);

	UE_LOG(LogTemp, Log,
		TEXT("[WorldLore] Generation complete. %s"),
		*Context.LoreState.GetSummaryString());
}
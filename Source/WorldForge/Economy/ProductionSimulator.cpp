#include "Economy/ProductionSimulator.h"

#include "Math/UnrealMathUtility.h"

float UProductionSimulator::ComputeOutput(const FProductionInputs& In) const
{
	if (In.WorkerCount <= 0)
	{
		return 0.0f;
	}

	const float SkillMod = FMath::Clamp(In.SkillMod, 0.0f, 2.0f);
	const float BuildingMod = FMath::Clamp(In.BuildingMod, 0.0f, 2.0f);
	const float BiomeMod = FMath::Clamp(In.BiomeMod, 0.0f, 2.0f);
	const float Disruption = FMath::Clamp(In.DisruptionFactor, 0.0f, 1.0f);
	const float CosmicMod = FMath::Clamp(In.CosmicMod, 0.0f, 2.0f);

	const float RawOutput =
		static_cast<float>(In.WorkerCount) *
		SkillMod *
		BuildingMod *
		BiomeMod *
		Disruption *
		CosmicMod;

	// Deterministic discrete output: floor before returning/storing.
	return FMath::Max(0.0f, FMath::FloorToFloat(RawOutput));
}
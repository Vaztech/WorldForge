#include "Economy/ConsumptionSimulator.h"

#include "Math/UnrealMathUtility.h"

float UConsumptionSimulator::ComputeDemand(const FConsumptionInputs& In) const
{
	if (In.Population <= 0)
	{
		return 0.0f;
	}

	const float NeedWeight = FMath::Clamp(In.NeedWeight, 0.0f, 1.0f);
	const float SeasonMod = FMath::Clamp(In.SeasonMod, 0.1f, 3.0f);
	const float CulturalMod = FMath::Clamp(In.CulturalMod, 0.1f, 2.0f);
	const float PriceElasticity = FMath::Clamp(In.PriceElasticity, 0.0f, 3.0f);

	const float RawDemand =
		static_cast<float>(In.Population) *
		NeedWeight *
		SeasonMod *
		CulturalMod *
		PriceElasticity;

	// Deterministic discrete demand: floor before returning/storing.
	return FMath::Max(0.0f, FMath::FloorToFloat(RawDemand));
}
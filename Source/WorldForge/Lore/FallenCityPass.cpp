// FallenCityPass.cpp — Fallen city / ruin generation

#include "FallenCityPass.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogFallenCity, Log, All);

// ─────────────────────────────────────────────
//  Ruin name fragments
// ─────────────────────────────────────────────

namespace RuinNames
{
	static const TArray<FString> Prefixes = {
		TEXT("Old"), TEXT("Lost"), TEXT("Fallen"), TEXT("Silent"),
		TEXT("Shattered"), TEXT("Forgotten"), TEXT("Hollow"), TEXT("Ashen"),
		TEXT("Cursed"), TEXT("Sunken"), TEXT("Withered"), TEXT("Broken")
	};

	static const TArray<FString> Cores = {
		TEXT("Khal"), TEXT("Meren"), TEXT("Dur"), TEXT("Ashara"),
		TEXT("Voss"), TEXT("Thane"), TEXT("Ulm"), TEXT("Pyra"),
		TEXT("Gorth"), TEXT("Selene"), TEXT("Krest"), TEXT("Nava")
	};

	static const TArray<FString> Suffixes = {
		TEXT("hold"), TEXT("spire"), TEXT("gate"), TEXT("reach"),
		TEXT("fall"), TEXT("tomb"), TEXT("deep"), TEXT("vale"),
		TEXT("ruin"), TEXT("keep"), TEXT("hollow"), TEXT("rest")
	};
}

// ─────────────────────────────────────────────
//  Execute
// ─────────────────────────────────────────────

void FFallenCityPass::Execute(FLoreGenerationContext& Ctx)
{
	UE_LOG(LogFallenCity, Log, TEXT("FallenCityPass — BEGIN (%d civs)"),
		Ctx.Civilizations.Num());

	int32 CityIndex = 0;

	for (const FCivilizationData& Civ : Ctx.Civilizations)
	{
		int32 NumRuins = RollFallenCityCount(Civ, Ctx.RNG);

		for (int32 i = 0; i < NumRuins; ++i)
		{
			FLoreFallenCity City;
			City.CityID = MakeCityID(CityIndex++);
			City.OriginalCivID = Civ.CivID;
			City.DisplayName = GenerateRuinName(Civ, Ctx.RNG);

			// Founded sometime in the civ's active period
			int32 CivStart = FMath::Max(0, Civ.FoundedEpoch);
			int32 CivEnd = (Civ.CollapseEpoch >= 0) ? Civ.CollapseEpoch : Ctx.NumEpochs() - 1;

			if (CivEnd > CivStart)
			{
				City.FoundedInEpoch = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, CivStart, CivEnd - 1);
				City.FellInEpoch = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, City.FoundedInEpoch + 1, CivEnd);
			}
			else
			{
				City.FoundedInEpoch = CivStart;
				City.FellInEpoch = CivStart;
			}

			// Pick a conqueror (50% chance it was another civ, if any exist)
			const FCivilizationData* Conqueror = nullptr;
			bool bHasConqueror = false;
			if (Ctx.Civilizations.Num() > 1 && Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY) < 0.50f)
			{
				// Pick a random different civ that was alive at the time
				TArray<const FCivilizationData*> Candidates;
				for (const FCivilizationData& Other : Ctx.Civilizations)
				{
					if (Other.CivID != Civ.CivID
						&& Other.FoundedEpoch <= City.FellInEpoch
						&& (Other.CollapseEpoch < 0 || Other.CollapseEpoch >= City.FellInEpoch))
					{
						Candidates.Add(&Other);
					}
				}
				if (Candidates.Num() > 0)
				{
					int32 Pick = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Candidates.Num() - 1);
					Conqueror = Candidates[Pick];
					City.ConquerorCivID = Conqueror->CivID;
					bHasConqueror = true;
				}
			}

			City.CauseOfFall = RollCauseOfFall(Civ, bHasConqueror, Ctx.RNG);

			// Place the ruin on the map
			City.TileCoord = ChooseRuinTile(Civ, Ctx);

			// Pull biome from tile
			const FWorldTileData* Tile = Ctx.GetTile(City.TileCoord.X, City.TileCoord.Y);
			if (Tile)
			{
				City.BiomeTag = Tile->BiomeType;
			}

			// Danger rating: wars produce more dangerous ruins, old ruins accumulate danger
			float AgeFactor = FMath::Clamp(
				static_cast<float>(Ctx.NumEpochs() - City.FellInEpoch) / FMath::Max(1, Ctx.NumEpochs()),
				0.0f, 1.0f);
			City.DangerRating = FMath::Clamp(
				0.2f + AgeFactor * 0.3f + (bHasConqueror ? 0.2f : 0.0f) + Ctx.RNG.NextFloatRange(UWorldForgeRNG::BRANCH_PREHISTORY, 0.0f, 0.2f),
				0.0f, 1.0f);

			// Bury artifacts: any artifact held by this civ may end up here
			for (FLoreArtifact& Artifact : Ctx.LoreState.Artifacts)
			{
				if (Artifact.CurrentHolderCivID == Civ.CivID
					&& Artifact.LostInCityID.IsNone()
					&& Artifact.ForgedInEpoch <= City.FellInEpoch
					&& Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY) < 0.35f)
				{
					Artifact.LostInCityID = City.CityID;
					Artifact.CurrentHolderCivID = -1; // No longer held
					City.ArtifactsLostHere.Add(Artifact.ArtifactID);
				}
			}

			City.LoreSnippet = GenerateLoreSnippet(City, Civ, Conqueror);

			Ctx.LoreState.FallenCities.Add(MoveTemp(City));
		}
	}

	UE_LOG(LogFallenCity, Log, TEXT("FallenCityPass — END | %d fallen cities generated"),
		Ctx.LoreState.FallenCities.Num());
}

// ─────────────────────────────────────────────
//  How many ruins per civ
// ─────────────────────────────────────────────

int32 FFallenCityPass::RollFallenCityCount(const FCivilizationData& Civ, UWorldForgeRNG& RNG)
{
	// Collapsed civs leave more ruins
	int32 Base = (Civ.CollapseEpoch >= 0) ? 2 : 1;

	// Large civs (many settlements) leave more
	Base += Civ.Settlements.Num() / 4;

	// Random variance ±1
	Base += RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, -1, 1);

	return FMath::Clamp(Base, 0, 5);
}

// ─────────────────────────────────────────────
//  Cause of fall
// ─────────────────────────────────────────────

ECityFallCause FFallenCityPass::RollCauseOfFall(
	const FCivilizationData& Civ, bool bHasConqueror, UWorldForgeRNG& RNG)
{
	if (bHasConqueror)
	{
		// Conqueror present — heavily favor War, but allow CivilWar
		return (RNG.NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY) < 0.85f) ? ECityFallCause::War : ECityFallCause::CivilWar;
	}

	// No conqueror — weighted random from non-war causes
	float Roll = RNG.NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
	if (Roll < 0.25f) return ECityFallCause::Plague;
	if (Roll < 0.45f) return ECityFallCause::Famine;
	if (Roll < 0.65f) return ECityFallCause::NaturalDisaster;
	if (Roll < 0.80f) return ECityFallCause::CosmicWrath;
	if (Roll < 0.90f) return ECityFallCause::CivilWar;
	return ECityFallCause::Abandonment;
}

// ─────────────────────────────────────────────
//  Ruin placement
// ─────────────────────────────────────────────

FIntPoint FFallenCityPass::ChooseRuinTile(
	const FCivilizationData& Civ,
	const FLoreGenerationContext& Ctx)
{
	// Prefer tiles from the civ's settlement list (outer settlements = frontier = more likely to fall)
	if (Civ.Settlements.Num() > 0)
	{
		// Pick from the second half of settlements (later-founded, more vulnerable)
		int32 StartIdx = FMath::Max(0, Civ.Settlements.Num() / 2);
		int32 Pick = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, StartIdx, Civ.Settlements.Num() - 1);
		return Civ.Settlements[Pick].TileCoord;
	}

	// Fallback: random tile near world center
	return FIntPoint(
		Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, Ctx.WorldWidth / 4, 3 * Ctx.WorldWidth / 4),
		Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, Ctx.WorldHeight / 4, 3 * Ctx.WorldHeight / 4)
	);
}

// ─────────────────────────────────────────────
//  Lore snippet
// ─────────────────────────────────────────────

FText FFallenCityPass::GenerateLoreSnippet(
	const FLoreFallenCity& City,
	const FCivilizationData& OrigCiv,
	const FCivilizationData* Conqueror)
{
	FString Snippet;

	switch (City.CauseOfFall)
	{
	case ECityFallCause::War:
		if (Conqueror)
		{
			Snippet = FString::Printf(
				TEXT("%s, once a proud city of the %s, was razed by the armies of the %s in epoch %d. Its walls still bear the scars of siege."),
				*City.DisplayName.ToString(), *OrigCiv.Name,
				*Conqueror->Name, City.FellInEpoch);
		}
		else
		{
			Snippet = FString::Printf(
				TEXT("%s fell to war in epoch %d. None remain to tell who struck the final blow."),
				*City.DisplayName.ToString(), City.FellInEpoch);
		}
		break;

	case ECityFallCause::Plague:
		Snippet = FString::Printf(
			TEXT("A great sickness swept through %s in epoch %d, leaving only silence and bone."),
			*City.DisplayName.ToString(), City.FellInEpoch);
		break;

	case ECityFallCause::Famine:
		Snippet = FString::Printf(
			TEXT("When the harvests failed for the third year, the people of %s simply walked away. Epoch %d was its last."),
			*City.DisplayName.ToString(), City.FellInEpoch);
		break;

	case ECityFallCause::NaturalDisaster:
		Snippet = FString::Printf(
			TEXT("The earth itself devoured %s in epoch %d. Whether quake or flood, the result was the same."),
			*City.DisplayName.ToString(), City.FellInEpoch);
		break;

	case ECityFallCause::CosmicWrath:
		Snippet = FString::Printf(
			TEXT("The sky above %s split open in epoch %d. Whatever descended left nothing alive. The %s do not speak of it."),
			*City.DisplayName.ToString(), City.FellInEpoch, *OrigCiv.Name);
		break;

	case ECityFallCause::CivilWar:
		Snippet = FString::Printf(
			TEXT("The %s turned upon themselves at %s in epoch %d. Brother slew brother until the city was ash."),
			*OrigCiv.Name, *City.DisplayName.ToString(), City.FellInEpoch);
		break;

	case ECityFallCause::Abandonment:
		Snippet = FString::Printf(
			TEXT("No catastrophe felled %s. Its people simply left, one family at a time, until only the wind walked its streets."),
			*City.DisplayName.ToString());
		break;
	}

	// Mention buried artifacts
	if (City.ArtifactsLostHere.Num() > 0)
	{
		Snippet += FString::Printf(
			TEXT(" Treasure hunters whisper that %d artifact%s still %s within its depths."),
			City.ArtifactsLostHere.Num(),
			City.ArtifactsLostHere.Num() > 1 ? TEXT("s") : TEXT(""),
			City.ArtifactsLostHere.Num() > 1 ? TEXT("lie") : TEXT("lies"));
	}

	return FText::FromString(Snippet);
}

// ─────────────────────────────────────────────
//  Name generation
// ─────────────────────────────────────────────

FText FFallenCityPass::GenerateRuinName(const FCivilizationData& Civ, UWorldForgeRNG& RNG)
{
	const auto& P = RuinNames::Prefixes;
	const auto& C = RuinNames::Cores;
	const auto& S = RuinNames::Suffixes;

	int32 PIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, P.Num() - 1);
	int32 CIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, C.Num() - 1);
	int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, S.Num() - 1);

	FString Name = FString::Printf(TEXT("%s %s%s"), *P[PIdx], *C[CIdx], *S[SIdx]);
	return FText::FromString(Name);
}

FName FFallenCityPass::MakeCityID(int32 Index)
{
	return FName(*FString::Printf(TEXT("FallenCity_%04d"), Index));
}
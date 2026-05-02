// WorldLoreTypes.cpp — Lookup implementations for FWorldLoreState

#include "WorldLoreTypes.h"

// ─────────────────────────────────────────────
//  Single-item lookups
// ─────────────────────────────────────────────

const FLoreLineage* FWorldLoreState::FindLineage(FName ID) const
{
	return Lineages.FindByPredicate([ID](const FLoreLineage& L)
		{
			return L.LineageID == ID;
		});
}

const FLoreBattle* FWorldLoreState::FindBattle(FName ID) const
{
	return Battles.FindByPredicate([ID](const FLoreBattle& B)
		{
			return B.BattleID == ID;
		});
}

const FLoreArtifact* FWorldLoreState::FindArtifact(FName ID) const
{
	return Artifacts.FindByPredicate([ID](const FLoreArtifact& A)
		{
			return A.ArtifactID == ID;
		});
}

const FLoreCreature* FWorldLoreState::FindCreature(FName ID) const
{
	return Creatures.FindByPredicate([ID](const FLoreCreature& C)
		{
			return C.CreatureID == ID;
		});
}

const FLoreFallenCity* FWorldLoreState::FindFallenCity(FName ID) const
{
	return FallenCities.FindByPredicate([ID](const FLoreFallenCity& C)
		{
			return C.CityID == ID;
		});
}

const FLoreLegend* FWorldLoreState::FindLegend(FName ID) const
{
	return Legends.FindByPredicate([ID](const FLoreLegend& L)
		{
			return L.LegendID == ID;
		});
}

const FColonistBackstory* FWorldLoreState::FindColonist(FName ID) const
{
	return ColonistBackstories.FindByPredicate([ID](const FColonistBackstory& C)
		{
			return C.ColonistID == ID;
		});
}

// ─────────────────────────────────────────────
//  Filtered queries
// ─────────────────────────────────────────────

TArray<const FLoreArtifact*> FWorldLoreState::GetArtifactsByCiv(int32 CivID) const
{
	TArray<const FLoreArtifact*> Result;

	for (const FLoreArtifact& A : Artifacts)
	{
		if (A.ForgedByCivID == CivID)
		{
			Result.Add(&A);
		}
	}

	return Result;
}

TArray<const FLoreFallenCity*> FWorldLoreState::GetFallenCitiesByCiv(int32 CivID) const
{
	TArray<const FLoreFallenCity*> Result;

	for (const FLoreFallenCity& C : FallenCities)
	{
		if (C.OriginalCivID == CivID)
		{
			Result.Add(&C);
		}
	}

	return Result;
}

TArray<const FLoreLegend*> FWorldLoreState::GetLegendsByEpoch(int32 Epoch) const
{
	TArray<const FLoreLegend*> Result;

	for (const FLoreLegend& L : Legends)
	{
		if (L.Epoch == Epoch)
		{
			Result.Add(&L);
		}
	}

	return Result;
}

// ─────────────────────────────────────────────
//  Debug
// ─────────────────────────────────────────────

FString FWorldLoreState::GetSummaryString() const
{
	return FString::Printf(
		TEXT("WorldLoreState | Seed:%d | Lineages:%d | Battles:%d | Artifacts:%d | Creatures:%d | FallenCities:%d | Legends:%d | Colonists:%d"),
		LoreSeed,
		Lineages.Num(),
		Battles.Num(),
		Artifacts.Num(),
		Creatures.Num(),
		FallenCities.Num(),
		Legends.Num(),
		ColonistBackstories.Num()
	);
}

void FWorldLoreState::LogFullLore() const
{
	UE_LOG(LogTemp, Log, TEXT("========== WORLD LORE FULL DUMP =========="));
	UE_LOG(LogTemp, Log, TEXT("%s"), *GetSummaryString());

	UE_LOG(LogTemp, Log, TEXT("---- ARTIFACTS (%d) ----"), Artifacts.Num());
	for (const FLoreArtifact& A : Artifacts)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Artifact] ID=%s Name=\"%s\" Type=%s Civ=%d Figure=%d Epoch=%d Holder=%d Power=%.2f Traits=%s Desc=\"%s\""),
			*A.ArtifactID.ToString(),
			*A.DisplayName.ToString(),
			*A.ArtifactType.ToString(),
			A.ForgedByCivID,
			A.ForgedByFigureID,
			A.ForgedInEpoch,
			A.CurrentHolderCivID,
			A.PowerRating,
			*A.TraitTags.ToStringSimple(),
			*A.Description.ToString());
	}

	UE_LOG(LogTemp, Log, TEXT("---- FALLEN CITIES (%d) ----"), FallenCities.Num());
	for (const FLoreFallenCity& C : FallenCities)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[FallenCity] ID=%s Name=\"%s\" Tile=(%d,%d) OriginalCiv=%d Conqueror=%d FoundedEpoch=%d FellEpoch=%d Cause=%d Danger=%.2f Biome=%s Lore=\"%s\""),
			*C.CityID.ToString(),
			*C.DisplayName.ToString(),
			C.TileCoord.X,
			C.TileCoord.Y,
			C.OriginalCivID,
			C.ConquerorCivID,
			C.FoundedInEpoch,
			C.FellInEpoch,
			static_cast<int32>(C.CauseOfFall),
			C.DangerRating,
			*C.BiomeTag.ToString(),
			*C.LoreSnippet.ToString());
	}

	UE_LOG(LogTemp, Log, TEXT("---- LEGENDS (%d) ----"), Legends.Num());
	for (const FLoreLegend& L : Legends)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Legend] ID=%s Title=\"%s\" Type=%d Domain=%s Epoch=%d Renown=%.2f CivRefs=%d ArtifactRefs=%d CityRefs=%d FigureRefs=%d Text=\"%s\""),
			*L.LegendID.ToString(),
			*L.Title.ToString(),
			static_cast<int32>(L.LegendType),
			*L.CosmicDomain.ToString(),
			L.Epoch,
			L.Renown,
			L.ReferencedCivIDs.Num(),
			L.ReferencedArtifactIDs.Num(),
			L.ReferencedCityIDs.Num(),
			L.ReferencedFigureIDs.Num(),
			*L.NarrativeText.ToString());
	}

	UE_LOG(LogTemp, Log, TEXT("---- COLONIST BACKSTORIES (%d) ----"), ColonistBackstories.Num());
	for (const FColonistBackstory& B : ColonistBackstories)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ColonistBackstory] ID=%s Name=\"%s\" HomeCiv=%d Archetype=%d Traits=%s Combat=%.2f Craft=%.2f Social=%.2f Scholar=%.2f KnownArtifacts=%d KnownLegends=%d Text=\"%s\""),
			*B.ColonistID.ToString(),
			*B.DisplayName.ToString(),
			B.HomeCivID,
			static_cast<int32>(B.Archetype),
			*B.PersonalityTraits.ToStringSimple(),
			B.CombatAptitude,
			B.CraftingAptitude,
			B.SocialAptitude,
			B.ScholarAptitude,
			B.KnownArtifactIDs.Num(),
			B.KnownLegendIDs.Num(),
			*B.BackstoryText.ToString());
	}

	UE_LOG(LogTemp, Log, TEXT("========== END WORLD LORE FULL DUMP =========="));
}
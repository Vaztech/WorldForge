#include "UI/WorldForgeEmbarkationManager.h"

#include "WorldForgeTags.h"

namespace
{
	constexpr const TCHAR* DefaultSaveSlotName = TEXT("WorldForge_Save");

	bool IsAllowedSaveSlotCharacter(const TCHAR Character)
	{
		return FChar::IsAlnum(Character)
			|| Character == TCHAR('_')
			|| Character == TCHAR('-');
	}

	bool ContainsLetterOrNumber(const FString& Value)
	{
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			if (FChar::IsAlnum(Value[Index]))
			{
				return true;
			}
		}

		return false;
	}

	FString HumanizeSeedIdentifier(const FString& Identifier)
	{
		FString Result;
		Result.Reserve(Identifier.Len() + 4);

		for (int32 Index = 0; Index < Identifier.Len(); ++Index)
		{
			const TCHAR Character = Identifier[Index];
			const TCHAR PreviousCharacter = Index > 0 ? Identifier[Index - 1] : 0;
			if (Index > 0 && FChar::IsUpper(Character) && (FChar::IsLower(PreviousCharacter) || FChar::IsDigit(PreviousCharacter)))
			{
				Result.AppendChar(TCHAR(' '));
			}
			Result.AppendChar(Character);
		}

		return Result;
	}

	FGameplayTag RequestOptionalItemTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	FWorldForgeSimpleEmbarkLoadoutOption MakeSimpleLoadoutOption(
		const TCHAR* ItemId,
		const TCHAR* DisplayName,
		const TCHAR* TagName,
		const int32 PointCost)
	{
		FWorldForgeSimpleEmbarkLoadoutOption Option;
		Option.ItemId = FName(ItemId);
		Option.DisplayName = FText::FromString(DisplayName);
		Option.ItemTag = RequestOptionalItemTag(TagName);
		Option.PointCost = FMath::Max(0, PointCost);
		return Option;
	}

	const TArray<FWorldForgeSimpleEmbarkLoadoutOption>& GetSimpleLoadoutCatalog()
	{
		static const TArray<FWorldForgeSimpleEmbarkLoadoutOption> Catalog =
		{
			MakeSimpleLoadoutOption(TEXT("FoodRations"), TEXT("Food Rations"), TEXT("WorldForge.Item.Food"), 1),
			MakeSimpleLoadoutOption(TEXT("Wood"), TEXT("Wood"), TEXT("WorldForge.Item.Wood"), 1),
			MakeSimpleLoadoutOption(TEXT("Stone"), TEXT("Stone"), TEXT("WorldForge.Item.Stone"), 1),
			MakeSimpleLoadoutOption(TEXT("Seeds"), TEXT("Seeds"), TEXT("WorldForge.Item.Seed.Grain"), 2),
			MakeSimpleLoadoutOption(TEXT("Pickaxe"), TEXT("Pickaxe"), TEXT("WorldForge.Item.Tool.Pickaxe"), 5),
			MakeSimpleLoadoutOption(TEXT("Medicine"), TEXT("Medicine"), TEXT("WorldForge.Item.Medicine"), 8),
			MakeSimpleLoadoutOption(TEXT("Cloth"), TEXT("Cloth"), TEXT("WorldForge.Item.Cloth"), 2),
			MakeSimpleLoadoutOption(TEXT("CopperIngots"), TEXT("Copper Ingots"), TEXT("WorldForge.Item.Metal.CopperIngots"), 3)
		};

		return Catalog;
	}

	// The 8 visual-backed playable embark races. Human is intentionally excluded.
	const TArray<FName>& GetPlayableEmbarkRaceTagNames()
	{
		static const TArray<FName> Names =
		{
			FName("WorldForge.Race.Dwarf"),
			FName("WorldForge.Race.HighElf"),
			FName("WorldForge.Race.ConstructFolk"),
			FName("WorldForge.Race.Namekian"),
			FName("WorldForge.Race.FaeKin"),
			FName("WorldForge.Race.Tidecaller"),
			FName("WorldForge.Race.Oni"),
			FName("WorldForge.Race.DarkElf"),
		};
		return Names;
	}

	bool DoesSelectedItemMatchOption(const FEmbarkSelectedItem& Item, const FWorldForgeSimpleEmbarkLoadoutOption& Option)
	{
		if (!Item.ItemId.IsNone() && Item.ItemId == Option.ItemId)
		{
			return true;
		}

		if (!Item.ItemId.IsNone())
		{
			return false;
		}

		if (Option.ItemTag.IsValid() && Item.ItemTag == Option.ItemTag)
		{
			return true;
		}

		return Item.DisplayName.ToString().Equals(Option.DisplayName.ToString(), ESearchCase::CaseSensitive);
	}

	// ---------------------------------------------------------------------------
	// Colonist generation helpers
	// ---------------------------------------------------------------------------

	struct FColonistAgeRange { int32 Min; int32 Max; };

	FColonistAgeRange GetColonistAgeRange(const FGameplayTag& RaceTag)
	{
		const FName T = RaceTag.GetTagName();
		if (T == FName("WorldForge.Race.Dwarf"))        return {  40, 180 };
		if (T == FName("WorldForge.Race.HighElf"))       return {  80, 500 };
		if (T == FName("WorldForge.Race.ConstructFolk")) return {   1, 200 };
		if (T == FName("WorldForge.Race.Namekian"))      return {  10, 150 };
		if (T == FName("WorldForge.Race.FaeKin"))        return {  20, 400 };
		if (T == FName("WorldForge.Race.Tidecaller"))    return {  18, 220 };
		if (T == FName("WorldForge.Race.Oni"))           return {  20, 250 };
		if (T == FName("WorldForge.Race.DarkElf"))       return {  70, 450 };
		return { 18, 80 };
	}

	FString GetColonistRaceDisplayName(const FGameplayTag& RaceTag)
	{
		const FName T = RaceTag.GetTagName();
		if (T == FName("WorldForge.Race.Dwarf"))        return TEXT("Dwarf");
		if (T == FName("WorldForge.Race.HighElf"))       return TEXT("High Elf");
		if (T == FName("WorldForge.Race.ConstructFolk")) return TEXT("Construct Folk");
		if (T == FName("WorldForge.Race.Namekian"))      return TEXT("Namekian");
		if (T == FName("WorldForge.Race.FaeKin"))        return TEXT("Fae Kin");
		if (T == FName("WorldForge.Race.Tidecaller"))    return TEXT("Tidecaller");
		if (T == FName("WorldForge.Race.Oni"))           return TEXT("Oni");
		if (T == FName("WorldForge.Race.DarkElf"))       return TEXT("Dark Elf");
		return TEXT("Colonist");
	}

	const TArray<FString>& GetColonistNamePool(const FGameplayTag& RaceTag)
	{
		const FName T = RaceTag.GetTagName();

		if (T == FName("WorldForge.Race.Dwarf"))
		{
			static const TArray<FString> N = {
				TEXT("Thordak"), TEXT("Grudnak"),  TEXT("Molvara"), TEXT("Bekka"),
				TEXT("Borin"),   TEXT("Helda"),    TEXT("Grunholt"), TEXT("Dagni"),
				TEXT("Thorvald"), TEXT("Mirva"),   TEXT("Beldra"),  TEXT("Stonekin")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.HighElf"))
		{
			static const TArray<FString> N = {
				TEXT("Aerindel"), TEXT("Sylvara"), TEXT("Thalos"),   TEXT("Elowen"),
				TEXT("Caelith"),  TEXT("Liriel"),  TEXT("Vaelthas"), TEXT("Eriwyn"),
				TEXT("Faendal"),  TEXT("Sylvana"), TEXT("Aelindra"), TEXT("Caeluven")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.ConstructFolk"))
		{
			// Patch 5N-I: Replaced serial/numeric placeholder names with readable fantasy-mechanical names.
			static const TArray<FString> N = {
				TEXT("Auric Ironveil"),     TEXT("Ferrum Copperwake"), TEXT("Caldus Brassroot"),
				TEXT("Veltrix Gearwyn"),    TEXT("Orison Anvilborn"),  TEXT("Brindle Hearthgear"),
				TEXT("Asterion Runeplate"), TEXT("Corven Cindersoul"), TEXT("Thalen Stonecoil"),
				TEXT("Edrik Emberframe"),   TEXT("Caldus Ironveil"),   TEXT("Veltrix Copperwake")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.Namekian"))
		{
			static const TArray<FString> N = {
				TEXT("Kodo"),  TEXT("Pelo"),  TEXT("Vren"),  TEXT("Ixar"),
				TEXT("Fylo"),  TEXT("Narek"), TEXT("Tova"),  TEXT("Xelu"),
				TEXT("Drevo"), TEXT("Sorka"), TEXT("Irven"), TEXT("Tyxal")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.FaeKin"))
		{
			static const TArray<FString> N = {
				TEXT("Pixie"),    TEXT("Wisp"),       TEXT("Lumara"),     TEXT("Twindle"),
				TEXT("Glimmer"),  TEXT("Sparrow"),    TEXT("Nettlewind"), TEXT("Briarbelle"),
				TEXT("Dewdrop"),  TEXT("Sundance"),   TEXT("Mosswhirl"),  TEXT("Fernlark")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.Tidecaller"))
		{
			static const TArray<FString> N = {
				TEXT("Corael"),  TEXT("Waveborn"), TEXT("Sealith"),  TEXT("Tidalyn"),
				TEXT("Driftan"), TEXT("Mareka"),   TEXT("Surgeon"),  TEXT("Ripthorn"),
				TEXT("Brinova"), TEXT("Coralith"), TEXT("Driftane"), TEXT("Waveka")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.Oni"))
		{
			static const TArray<FString> N = {
				TEXT("Grask"),   TEXT("Toruk"),  TEXT("Vreth"),  TEXT("Karog"),
				TEXT("Drekash"), TEXT("Murak"),  TEXT("Zorath"), TEXT("Borrak"),
				TEXT("Thraka"),  TEXT("Gurnak"), TEXT("Vrorsh"), TEXT("Kazrug")
			};
			return N;
		}
		if (T == FName("WorldForge.Race.DarkElf"))
		{
			static const TArray<FString> N = {
				TEXT("Xilara"),  TEXT("Veth"),   TEXT("Drovak"),  TEXT("Shivara"),
				TEXT("Noxen"),   TEXT("Caldris"), TEXT("Umbri"),  TEXT("Sylthas"),
				TEXT("Vadrith"), TEXT("Solthar"), TEXT("Nexara"), TEXT("Zylveth")
			};
			return N;
		}

		static const TArray<FString> Fallback = {
			TEXT("Aldric"), TEXT("Mira"),  TEXT("Torven"), TEXT("Syla"),
			TEXT("Drev"),   TEXT("Kara"),  TEXT("Zeth"),   TEXT("Lyra"),
			TEXT("Bron"),   TEXT("Vena"),  TEXT("Cael"),   TEXT("Thora")
		};
		return Fallback;
	}

	const TArray<FString>& GetColonistTraitPool()
	{
		// TODO: Data-drive from DT_WorldForgeTraits when trait row struct access is available.
		static const TArray<FString> Pool = {
			TEXT("Diligent"),       TEXT("Hardy"),          TEXT("Brave"),       TEXT("Curious"),
			TEXT("Patient"),        TEXT("Quick Reflexes"), TEXT("Creative"),    TEXT("Night Vision"),
			TEXT("Stubborn"),       TEXT("Calm"),           TEXT("Optimistic"),  TEXT("Cautious"),
			TEXT("Tough"),          TEXT("Green Thumb"),    TEXT("Inventive"),   TEXT("Natural Leader")
		};
		return Pool;
	}

	const TArray<FName>& GetCoreEmbarkRoles()
	{
		static const TArray<FName> Roles = {
			FName("Miner"), FName("Farmer"), FName("Crafter"), FName("Warrior"),
			FName("Scholar"), FName("Healer"), FName("Hauler"), FName("Builder")
		};
		return Roles;
	}

	const TArray<FName>& GetCoreEmbarkSkillNames()
	{
		// TODO: Data-drive from DT_WorldForgeSkills when skill row struct access is available.
		static const TArray<FName> Skills = {
			FName("Mining"), FName("Farming"), FName("Crafting"), FName("Combat"),
			FName("Medicine"), FName("Scholarship"), FName("Hauling"), FName("Building")
		};
		return Skills;
	}

	FName GetPrimarySkillForEmbarkRole(FName RoleId)
	{
		if (RoleId == FName("Miner"))     return FName("Mining");
		if (RoleId == FName("Farmer"))    return FName("Farming");
		if (RoleId == FName("Crafter"))   return FName("Crafting");
		if (RoleId == FName("Warrior"))   return FName("Combat");
		if (RoleId == FName("Scholar"))   return FName("Scholarship");
		if (RoleId == FName("Healer"))    return FName("Medicine");
		if (RoleId == FName("Hauler"))    return FName("Hauling");
		if (RoleId == FName("Builder"))   return FName("Building");
		return FName("Mining");
	}

	FString BuildColonistBackstory(const FString& Name, const FString& RaceDisplay,
		const FString& RoleStr, const FString& PrimaryTrait, const FString& PrimarySkillStr,
		int32 Variant)
	{
		switch (Variant % 4)
		{
		case 0:
			return FString::Printf(
				TEXT("Raised among the %s, %s discovered a gift for %s at an early age. Known for being %s, they became a skilled %s over many seasons."),
				*RaceDisplay, *Name, *PrimarySkillStr, *PrimaryTrait, *RoleStr);
		case 1:
			return FString::Printf(
				TEXT("%s, a %s of quiet resolve, walked the path of a %s long before others noticed. Their %s nature made them a reliable hand."),
				*Name, *RaceDisplay, *RoleStr, *PrimaryTrait);
		case 2:
			return FString::Printf(
				TEXT("Born to the %s, %s spent their formative years mastering %s. Their %s temperament earned them the role of %s."),
				*RaceDisplay, *Name, *PrimarySkillStr, *PrimaryTrait, *RoleStr);
		case 3:
		default:
			return FString::Printf(
				TEXT("%s left behind a quiet life among the %s to forge a new path as a %s. Considered %s by those who know them, their %s skills speak for themselves."),
				*Name, *RaceDisplay, *RoleStr, *PrimaryTrait, *PrimarySkillStr);
		}
	}

	bool AreSkillMapsEqual(const TMap<FName, int32>& Left, const TMap<FName, int32>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (const TPair<FName, int32>& Pair : Left)
		{
			const int32* RightValue = Right.Find(Pair.Key);
			if (!RightValue || *RightValue != Pair.Value)
			{
				return false;
			}
		}

		return true;
	}

	bool AreColonistProfilesEquivalent(const FWorldForgeEmbarkColonist& Left, const FWorldForgeEmbarkColonist& Right)
	{
		return Left.Name == Right.Name
			&& Left.RaceTag == Right.RaceTag
			&& Left.Age == Right.Age
			&& Left.Backstory == Right.Backstory
			&& Left.RoleId == Right.RoleId
			&& AreSkillMapsEqual(Left.Skills, Right.Skills)
			&& Left.Traits == Right.Traits
			&& Left.TraitTags == Right.TraitTags
			&& Left.Strength == Right.Strength
			&& Left.Dexterity == Right.Dexterity
			&& Left.Intelligence == Right.Intelligence
			&& Left.Endurance == Right.Endurance
			&& Left.Charisma == Right.Charisma;
	}

}

void UWorldForgeEmbarkationManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetEmbarkConfigToDefaults();
}

void UWorldForgeEmbarkationManager::SetWorldName(const FString& InWorldName)
{
	EmbarkConfig.WorldName = InWorldName.TrimStartAndEnd();
	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] WorldName set to '%s'"),
		*EmbarkConfig.WorldName);

	GenerateOrUpdateSaveSlotNameFromWorldName();
}

FString UWorldForgeEmbarkationManager::GetWorldName() const
{
	return EmbarkConfig.WorldName;
}

void UWorldForgeEmbarkationManager::SetSeedString(const FString& InSeedString)
{
	const FString TrimmedSeed = InSeedString.TrimStartAndEnd();
	EmbarkConfig.SeedString = TrimmedSeed.IsEmpty() ? TEXT("12345") : TrimmedSeed;
	EmbarkConfig.ResolvedSeed = ResolveSeedString(EmbarkConfig.SeedString);

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] SeedString changed to '%s' ResolvedSeed=%d"),
		*EmbarkConfig.SeedString,
		EmbarkConfig.ResolvedSeed);
}

FString UWorldForgeEmbarkationManager::GetSeedString() const
{
	return EmbarkConfig.SeedString;
}

int32 UWorldForgeEmbarkationManager::GetResolvedSeed() const
{
	return EmbarkConfig.ResolvedSeed != 0
		? EmbarkConfig.ResolvedSeed
		: ResolveSeedString(EmbarkConfig.SeedString);
}

void UWorldForgeEmbarkationManager::SetWorldSize(int32 InWorldSizeGrid)
{
	// Clamp to supported GDD world sizes.
	// Blueprint should send 128, 256, 512, or 1024.
	EmbarkConfig.WorldSizeGrid = FMath::Clamp(InWorldSizeGrid, 128, 1024);
	EmbarkConfig.WorldSize = EmbarkConfig.WorldSizeGrid;

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] WorldSizeGrid changed to %d"),
		EmbarkConfig.WorldSizeGrid);
}

int32 UWorldForgeEmbarkationManager::GetWorldSizeGrid() const
{
	return EmbarkConfig.WorldSizeGrid;
}

void UWorldForgeEmbarkationManager::GenerateOrUpdateSaveSlotNameFromWorldName()
{
	const FString TrimmedWorldName = EmbarkConfig.WorldName.TrimStartAndEnd();
	const FString NewSaveSlotName = MakeSafeSaveSlotName(TrimmedWorldName);

	EmbarkConfig.SaveSlotName = NewSaveSlotName;
	bHasUserEmbarkConfig = true;

	if (!TrimmedWorldName.Equals(NewSaveSlotName, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[EmbarkationManager] SaveSlotName sanitized from '%s' to '%s'"),
			*TrimmedWorldName,
			*NewSaveSlotName);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] SaveSlotName generated as '%s' from WorldName '%s'"),
		*EmbarkConfig.SaveSlotName,
		*EmbarkConfig.WorldName);
}

FString UWorldForgeEmbarkationManager::GetSaveSlotName() const
{
	return EmbarkConfig.SaveSlotName;
}

void UWorldForgeEmbarkationManager::SetColonyMode(EWorldForgeColonyMode InColonyMode)
{
	EmbarkConfig.ColonyMode = InColonyMode;
	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] ColonyMode changed to %s"),
		InColonyMode == EWorldForgeColonyMode::SingleRace ? TEXT("SingleRace") : TEXT("Mixed"));
}

EWorldForgeColonyMode UWorldForgeEmbarkationManager::GetColonyMode() const
{
	return EmbarkConfig.ColonyMode;
}

void UWorldForgeEmbarkationManager::SetSelectedRaceTag(FGameplayTag InRaceTag)
{
	EmbarkConfig.SelectedRaceTag = InRaceTag;
	EmbarkConfig.DefaultRaceTag = InRaceTag;
	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] SelectedRaceTag changed to '%s'"),
		*EmbarkConfig.SelectedRaceTag.ToString());
}

FGameplayTag UWorldForgeEmbarkationManager::GetSelectedRaceTag() const
{
	return EmbarkConfig.SelectedRaceTag;
}

bool UWorldForgeEmbarkationManager::IsWorldNameValid(FString& OutReason) const
{
	OutReason.Reset();

	const FString TrimmedWorldName = EmbarkConfig.WorldName.TrimStartAndEnd();
	if (TrimmedWorldName.IsEmpty())
	{
		OutReason = TEXT("World name is required.");
		return false;
	}

	if (!ContainsLetterOrNumber(TrimmedWorldName))
	{
		OutReason = TEXT("World name must contain at least one letter or number.");
		return false;
	}

	if (!ContainsLetterOrNumber(MakeSafeSaveSlotName(TrimmedWorldName)))
	{
		OutReason = TEXT("World name must contain at least one valid save-slot letter or number.");
		return false;
	}

	return true;
}

bool UWorldForgeEmbarkationManager::IsEmbarkWorldConfigValid(FString& OutReason) const
{
	if (!IsWorldNameValid(OutReason))
	{
		return false;
	}

	if (EmbarkConfig.SeedString.TrimStartAndEnd().IsEmpty())
	{
		OutReason = TEXT("Seed is required.");
		return false;
	}

	if (EmbarkConfig.WorldSizeGrid <= 0)
	{
		OutReason = TEXT("World size must be greater than zero.");
		return false;
	}

	if (EmbarkConfig.ColonyMode == EWorldForgeColonyMode::SingleRace && !EmbarkConfig.SelectedRaceTag.IsValid())
	{
		OutReason = TEXT("Single Race Colony requires a selected race.");
		return false;
	}

	if (EmbarkConfig.StartingColonistCount <= 0)
	{
		OutReason = TEXT("At least one starting colonist is required.");
		return false;
	}

	if (CalculateUsedEmbarkPoints() > EmbarkConfig.MaxEmbarkPoints)
	{
		OutReason = TEXT("Selected loadout exceeds available embark points.");
		return false;
	}

	OutReason.Reset();
	return true;
}

void UWorldForgeEmbarkationManager::ResetEmbarkConfigToDefaults()
{
	EmbarkConfig = FWorldForgeEmbarkConfig();
	EmbarkConfig.SeedString = TEXT("12345");
	EmbarkConfig.ResolvedSeed = ResolveSeedString(EmbarkConfig.SeedString);
	EmbarkConfig.WorldSizeGrid = 256;
	EmbarkConfig.WorldSize = EmbarkConfig.WorldSizeGrid;
	EmbarkConfig.SaveSlotName = MakeSafeSaveSlotName(EmbarkConfig.WorldName);
	EmbarkConfig.ColonyMode = EWorldForgeColonyMode::Mixed;
	EmbarkConfig.AllowedRaceTags = GetDefaultPlayableRaceTags();
	EmbarkConfig.SelectedRaceTag = EmbarkConfig.AllowedRaceTags.Num() > 0 ? EmbarkConfig.AllowedRaceTags[0] : FGameplayTag();
	EmbarkConfig.DefaultRaceTag = EmbarkConfig.SelectedRaceTag;
	EmbarkConfig.StartingColonistCount = 3;
	EmbarkConfig.MaxEmbarkPoints = 100;
	EmbarkConfig.UsedEmbarkPoints = 0;
	EmbarkConfig.SelectedLoadoutItems.Reset();
	StartingColonistRerollCounts.Reset();

	bHasUserEmbarkConfig = false;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Embark config reset to defaults. SeedString='%s' ResolvedSeed=%d WorldSizeGrid=%d SaveSlotName='%s'"),
		*EmbarkConfig.SeedString,
		EmbarkConfig.ResolvedSeed,
		EmbarkConfig.WorldSizeGrid,
		*EmbarkConfig.SaveSlotName);
}

void UWorldForgeEmbarkationManager::SetSelectedEmbarkTile(int32 InTileX, int32 InTileY, int32 InTileIndex)
{
	EmbarkConfig.SelectedEmbarkTileX = InTileX;
	EmbarkConfig.SelectedEmbarkTileY = InTileY;
	EmbarkConfig.SelectedEmbarkTileIndex = InTileIndex;
	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] SelectedEmbarkTile set: (%d,%d) Index=%d"),
		InTileX, InTileY, InTileIndex);
}

bool UWorldForgeEmbarkationManager::GetSelectedEmbarkTile(int32& OutTileX, int32& OutTileY, int32& OutTileIndex) const
{
	OutTileX = EmbarkConfig.SelectedEmbarkTileX;
	OutTileY = EmbarkConfig.SelectedEmbarkTileY;
	OutTileIndex = EmbarkConfig.SelectedEmbarkTileIndex;
	return OutTileX >= 0 && OutTileY >= 0 && OutTileIndex >= 0;
}

bool UWorldForgeEmbarkationManager::HasSelectedEmbarkTile() const
{
	return EmbarkConfig.SelectedEmbarkTileX >= 0 &&
		EmbarkConfig.SelectedEmbarkTileY >= 0 &&
		EmbarkConfig.SelectedEmbarkTileIndex >= 0;
}

void UWorldForgeEmbarkationManager::AddColonist(const FWorldForgeEmbarkColonist& InColonist)
{
	EmbarkConfig.StartingColonists.Add(InColonist);
	EmbarkConfig.StartingColonistCount = EmbarkConfig.StartingColonists.Num();

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Added colonist '%s'. Total=%d"),
		*InColonist.Name,
		EmbarkConfig.StartingColonists.Num());
}

void UWorldForgeEmbarkationManager::SetColonists(const TArray<FWorldForgeEmbarkColonist>& InColonists)
{
	EmbarkConfig.StartingColonists = InColonists;
	EmbarkConfig.StartingColonistCount = EmbarkConfig.StartingColonists.Num();
	StartingColonistRerollCounts.Reset();

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Colonists set. Total=%d"),
		EmbarkConfig.StartingColonists.Num());
}

void UWorldForgeEmbarkationManager::SetColonistRaceByIndex(int32 ColonistIndex, FGameplayTag RaceTag)
{
	if (!EmbarkConfig.StartingColonists.IsValidIndex(ColonistIndex) || !RaceTag.IsValid())
	{
		return;
	}

	EmbarkConfig.StartingColonists[ColonistIndex].RaceTag = RaceTag;
	bHasUserEmbarkConfig = true;
}

void UWorldForgeEmbarkationManager::SetDefaultRace(FGameplayTag RaceTag)
{
	if (!RaceTag.IsValid())
	{
		return;
	}

	SetSelectedRaceTag(RaceTag);
}

TArray<FGameplayTag> UWorldForgeEmbarkationManager::GetDefaultPlayableRaceTags() const
{
	// Returns only tags that resolve successfully against the registered tag table.
	// Human is not in GetPlayableEmbarkRaceTagNames() and is never added here.
	TArray<FGameplayTag> Tags;
	for (const FName& TagName : GetPlayableEmbarkRaceTagNames())
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
		if (Tag.IsValid())
		{
			Tags.Add(Tag);
		}
	}
	return Tags;
}

void UWorldForgeEmbarkationManager::EnsureDefaultColonists(int32 DesiredCount)
{
	DesiredCount = FMath::Max(0, DesiredCount);
	EmbarkConfig.StartingColonistCount = DesiredCount;

	const TArray<FGameplayTag> RaceCycle = EmbarkConfig.AllowedRaceTags.Num() > 0
		? EmbarkConfig.AllowedRaceTags
		: GetDefaultPlayableRaceTags();
	const FGameplayTag DefaultRace = EmbarkConfig.SelectedRaceTag.IsValid()
		? EmbarkConfig.SelectedRaceTag
		: TAG_Race_Dwarf;

	while (EmbarkConfig.StartingColonists.Num() < DesiredCount)
	{
		const int32 Index = EmbarkConfig.StartingColonists.Num();

		FWorldForgeEmbarkColonist Colonist;
		Colonist.Name = FString::Printf(TEXT("Colonist %d"), Index + 1);

		if (EmbarkConfig.ColonyMode == EWorldForgeColonyMode::SingleRace)
		{
			Colonist.RaceTag = DefaultRace;
		}
		else
		{
			Colonist.RaceTag = RaceCycle.IsValidIndex(Index % RaceCycle.Num())
				? RaceCycle[Index % RaceCycle.Num()]
				: DefaultRace;
		}

		EmbarkConfig.StartingColonists.Add(Colonist);
	}

	while (EmbarkConfig.StartingColonists.Num() > DesiredCount)
	{
		EmbarkConfig.StartingColonists.RemoveAt(EmbarkConfig.StartingColonists.Num() - 1);
	}

	for (FWorldForgeEmbarkColonist& Colonist : EmbarkConfig.StartingColonists)
	{
		if (!Colonist.RaceTag.IsValid() || EmbarkConfig.ColonyMode == EWorldForgeColonyMode::SingleRace)
		{
			Colonist.RaceTag = DefaultRace;
		}
	}
}

void UWorldForgeEmbarkationManager::GenerateStartingColonists()
{
	const int32 DesiredCount = FMath::Max(1, EmbarkConfig.StartingColonistCount);
	EmbarkConfig.StartingColonists.Reset();
	StartingColonistRerollCounts.Reset();

	for (int32 Index = 0; Index < DesiredCount; ++Index)
	{
		EmbarkConfig.StartingColonists.Add(GenerateStartingColonistProfile(Index, 0));
	}

	EmbarkConfig.StartingColonistCount = EmbarkConfig.StartingColonists.Num();
	bHasUserEmbarkConfig = true;

	const FString ModeStr = EmbarkConfig.ColonyMode == EWorldForgeColonyMode::SingleRace
		? TEXT("SingleRace") : TEXT("Mixed");
	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Generated starting colonists. Count=%d Mode=%s"),
		EmbarkConfig.StartingColonists.Num(), *ModeStr);

	for (int32 i = 0; i < EmbarkConfig.StartingColonists.Num(); ++i)
	{
		const FWorldForgeEmbarkColonist& C = EmbarkConfig.StartingColonists[i];
		UE_LOG(LogTemp, Log,
			TEXT("[EmbarkationManager] Colonist %d: Name=%s Race=%s Role=%s Age=%d"),
			i, *C.Name, *C.RaceTag.ToString(), *C.RoleId.ToString(), C.Age);

		if (C.Skills.Num() > 0)
		{
			FString SkillLog;
			for (const TPair<FName, int32>& Pair : C.Skills)
			{
				if (!SkillLog.IsEmpty()) SkillLog += TEXT(", ");
				SkillLog += FString::Printf(TEXT("%s=%d"), *Pair.Key.ToString(), Pair.Value);
			}
			UE_LOG(LogTemp, Log, TEXT("[EmbarkationManager] Skills: %s"), *SkillLog);
		}

		if (C.Traits.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[EmbarkationManager] Traits: %s"), *FString::Join(C.Traits, TEXT(", ")));
		}
	}
}

bool UWorldForgeEmbarkationManager::EnsureStartingColonistsGenerated()
{
	const int32 DesiredCount = FMath::Max(1, EmbarkConfig.StartingColonistCount);

	if (EmbarkConfig.StartingColonists.Num() == 0)
	{
		GenerateStartingColonists();
		UE_LOG(LogTemp, Log,
			TEXT("[EmbarkationManager] Generated missing starting colonists. Count=%d"),
			EmbarkConfig.StartingColonists.Num());
		return EmbarkConfig.StartingColonists.Num() > 0;
	}

	if (EmbarkConfig.StartingColonists.Num() < DesiredCount)
	{
		const int32 FirstMissingIndex = EmbarkConfig.StartingColonists.Num();
		for (int32 Index = FirstMissingIndex; Index < DesiredCount; ++Index)
		{
			EmbarkConfig.StartingColonists.Add(GenerateStartingColonistProfile(Index, 0));
			StartingColonistRerollCounts.Remove(Index);
		}

		EmbarkConfig.StartingColonistCount = EmbarkConfig.StartingColonists.Num();
		bHasUserEmbarkConfig = true;

		UE_LOG(LogTemp, Log,
			TEXT("[EmbarkationManager] Generated missing starting colonists. Count=%d"),
			EmbarkConfig.StartingColonists.Num());
		return EmbarkConfig.StartingColonists.Num() > 0;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Starting colonists already generated. Count=%d"),
		EmbarkConfig.StartingColonists.Num());
	return EmbarkConfig.StartingColonists.Num() > 0;
}

bool UWorldForgeEmbarkationManager::RerollStartingColonist(int32 ColonistIndex)
{
	EnsureStartingColonistsGenerated();

	if (!EmbarkConfig.StartingColonists.IsValidIndex(ColonistIndex))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkationManager] Reroll failed. Invalid index=%d"),
			ColonistIndex);
		return false;
	}

	const FWorldForgeEmbarkColonist PreviousColonist = EmbarkConfig.StartingColonists[ColonistIndex];
	FWorldForgeEmbarkColonist RerolledColonist;
	int32& RerollCount = StartingColonistRerollCounts.FindOrAdd(ColonistIndex);

	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		++RerollCount;
		RerolledColonist = GenerateStartingColonistProfile(ColonistIndex, RerollCount);
		if (!AreColonistProfilesEquivalent(PreviousColonist, RerolledColonist))
		{
			break;
		}
	}

	EmbarkConfig.StartingColonists[ColonistIndex] = RerolledColonist;
	bHasUserEmbarkConfig = true;

	const FWorldForgeEmbarkColonist& C = EmbarkConfig.StartingColonists[ColonistIndex];
	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Rerolled colonist Index=%d Name=%s Race=%s Role=%s"),
		ColonistIndex, *C.Name, *C.RaceTag.ToString(), *C.RoleId.ToString());

	return true;
}

TArray<FWorldForgeEmbarkColonist> UWorldForgeEmbarkationManager::GetStartingColonists() const
{
	return EmbarkConfig.StartingColonists;
}

bool UWorldForgeEmbarkationManager::GetStartingColonist(int32 ColonistIndex, FWorldForgeEmbarkColonist& OutColonist) const
{
	if (!EmbarkConfig.StartingColonists.IsValidIndex(ColonistIndex))
	{
		return false;
	}

	OutColonist = EmbarkConfig.StartingColonists[ColonistIndex];
	return true;
}

void UWorldForgeEmbarkationManager::ClearColonists()
{
	EmbarkConfig.StartingColonists.Reset();
	EmbarkConfig.StartingColonistCount = 0;
	StartingColonistRerollCounts.Reset();

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Colonists cleared."));
}

void UWorldForgeEmbarkationManager::ClearEmbarkConfig()
{
	ResetEmbarkConfigToDefaults();

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Embark config cleared."));
}

FWorldForgeEmbarkConfig UWorldForgeEmbarkationManager::GetEmbarkConfig() const
{
	FWorldForgeEmbarkConfig CurrentConfig = EmbarkConfig;
	CurrentConfig.UsedEmbarkPoints = CalculateUsedEmbarkPoints();
	return CurrentConfig;
}

int32 UWorldForgeEmbarkationManager::GetWorldSize() const
{
	return GetWorldSizeGrid();
}

bool UWorldForgeEmbarkationManager::HasUserEmbarkConfig() const
{
	return bHasUserEmbarkConfig;
}

TArray<FWorldForgeSimpleEmbarkLoadoutOption> UWorldForgeEmbarkationManager::GetStarterEmbarkLoadoutOptions() const
{
	TArray<FWorldForgeSimpleEmbarkLoadoutOption> Options = GetSimpleLoadoutCatalog();
	TArray<FWorldForgeSimpleEmbarkLoadoutOption> DynamicOptions;
	DynamicSeedLoadoutOptions.GenerateValueArray(DynamicOptions);
	Options.Append(DynamicOptions);
	return Options;
}

void UWorldForgeEmbarkationManager::RegisterDynamicSeedLoadoutOption(FName ItemId, const FText& DisplayName, const int32 PointCost)
{
	const FString ItemIdString = ItemId.ToString();
	if (ItemId.IsNone() || !ItemIdString.StartsWith(TEXT("Seed.")))
	{
		return;
	}

	FWorldForgeSimpleEmbarkLoadoutOption Option = MakeDynamicSeedOption(ItemId);
	if (!DisplayName.ToString().TrimStartAndEnd().IsEmpty())
	{
		Option.DisplayName = DisplayName;
	}
	Option.PointCost = FMath::Max(0, PointCost);
	DynamicSeedLoadoutOptions.Add(ItemId, Option);
}

int32 UWorldForgeEmbarkationManager::GetMaxEmbarkPoints() const
{
	return FMath::Max(0, EmbarkConfig.MaxEmbarkPoints);
}

int32 UWorldForgeEmbarkationManager::GetUsedEmbarkPoints() const
{
	return CalculateUsedEmbarkPoints();
}

int32 UWorldForgeEmbarkationManager::GetRemainingEmbarkPoints() const
{
	return FMath::Max(0, GetMaxEmbarkPoints() - GetUsedEmbarkPoints());
}

int32 UWorldForgeEmbarkationManager::GetEmbarkLoadoutItemQuantity(FName ItemId) const
{
	const int32 ExistingIndexByItemId = FindSelectedLoadoutItemIndexByItemId(ItemId);
	if (ExistingIndexByItemId != INDEX_NONE)
	{
		return FMath::Max(0, EmbarkConfig.SelectedLoadoutItems[ExistingIndexByItemId].Quantity);
	}

	const FWorldForgeSimpleEmbarkLoadoutOption* Option = FindStarterEmbarkLoadoutOption(ItemId);
	if (Option)
	{
		const int32 ExistingIndex = FindSelectedLoadoutItemIndex(*Option);
		return ExistingIndex != INDEX_NONE ? FMath::Max(0, EmbarkConfig.SelectedLoadoutItems[ExistingIndex].Quantity) : 0;
	}

	// Dynamic seed item
	const int32 ExistingIndex = FindSelectedLoadoutItemIndexByItemId(ItemId);
	return ExistingIndex != INDEX_NONE ? FMath::Max(0, EmbarkConfig.SelectedLoadoutItems[ExistingIndex].Quantity) : 0;
}

int32 UWorldForgeEmbarkationManager::GetEmbarkLoadoutItemCost(FName ItemId) const
{
	const FWorldForgeSimpleEmbarkLoadoutOption* Option = FindStarterEmbarkLoadoutOption(ItemId);
	if (Option)
	{
		return Option->PointCost;
	}

	// Dynamic seed items cost 2
	const FString ItemIdStr = ItemId.ToString();
	if (ItemIdStr.StartsWith(TEXT("Seed.")))
	{
		return 2;
	}
	return 0;
}

int32 UWorldForgeEmbarkationManager::GetEmbarkLoadoutItemTotalCost(FName ItemId) const
{
	return GetEmbarkLoadoutItemQuantity(ItemId) * GetEmbarkLoadoutItemCost(ItemId);
}

FText UWorldForgeEmbarkationManager::GetEmbarkLoadoutItemDisplayName(FName ItemId) const
{
	const FWorldForgeSimpleEmbarkLoadoutOption* Option = FindStarterEmbarkLoadoutOption(ItemId);
	if (Option)
	{
		return Option->DisplayName;
	}

	const FString ItemIdStr = ItemId.ToString();
	if (ItemIdStr.StartsWith(TEXT("Seed.")))
	{
		const FWorldForgeSimpleEmbarkLoadoutOption DynOption = MakeDynamicSeedOption(ItemId);
		return DynOption.DisplayName;
	}
	return FText::GetEmpty();
}

void UWorldForgeEmbarkationManager::ClearEmbarkLoadout()
{
	EmbarkConfig.SelectedLoadoutItems.Reset();
	RefreshUsedEmbarkPoints();
	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Cleared loadout."));
}

bool UWorldForgeEmbarkationManager::SetEmbarkLoadoutItemQuantity(FName ItemId, int32 Quantity)
{
	FWorldForgeSimpleEmbarkLoadoutOption Option;
	if (!TryResolveStarterEmbarkLoadoutOption(ItemId, Option))
	{
		return false;
	}

	const int32 NewQuantity = FMath::Max(0, Quantity);
	const int32 ExistingQuantity = GetEmbarkLoadoutItemQuantity(ItemId);
	const int32 ExistingUsedPoints = CalculateUsedEmbarkPoints();
	const int32 NewUsedPoints = ExistingUsedPoints - (ExistingQuantity * Option.PointCost) + (NewQuantity * Option.PointCost);

	if (NewUsedPoints > GetMaxEmbarkPoints())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Not enough embark points for %s."), *Option.DisplayName.ToString());
		return false;
	}

	int32 ExistingIndex = FindSelectedLoadoutItemIndexByItemId(ItemId);
	if (ExistingIndex == INDEX_NONE)
	{
		ExistingIndex = FindSelectedLoadoutItemIndex(Option);
	}

	if (NewQuantity <= 0)
	{
		if (ExistingIndex != INDEX_NONE)
		{
			EmbarkConfig.SelectedLoadoutItems.RemoveAt(ExistingIndex);
		}
	}
	else if (ExistingIndex != INDEX_NONE)
	{
		EmbarkConfig.SelectedLoadoutItems[ExistingIndex].ItemId = Option.ItemId;
		EmbarkConfig.SelectedLoadoutItems[ExistingIndex].ItemTag = Option.ItemTag;
		EmbarkConfig.SelectedLoadoutItems[ExistingIndex].Quantity = NewQuantity;
		EmbarkConfig.SelectedLoadoutItems[ExistingIndex].DisplayName = Option.DisplayName;
	}
	else
	{
		FEmbarkSelectedItem NewItem;
		NewItem.ItemId = Option.ItemId;
		NewItem.ItemTag = Option.ItemTag;
		NewItem.Quantity = NewQuantity;
		NewItem.DisplayName = Option.DisplayName;
		EmbarkConfig.SelectedLoadoutItems.Add(NewItem);
	}

	EmbarkConfig.UsedEmbarkPoints = NewUsedPoints;
	bHasUserEmbarkConfig = true;
	return true;
}

bool UWorldForgeEmbarkationManager::AddEmbarkLoadoutItem(FName ItemId)
{
	FWorldForgeSimpleEmbarkLoadoutOption Option;
	if (!TryResolveStarterEmbarkLoadoutOption(ItemId, Option))
	{
		return false;
	}

	const int32 NewQuantity = GetEmbarkLoadoutItemQuantity(ItemId) + 1;
	if (!SetEmbarkLoadoutItemQuantity(ItemId, NewQuantity))
	{
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkLoadout] Added %s Qty=%d"),
		*Option.DisplayName.ToString(),
		NewQuantity);
	return true;
}

bool UWorldForgeEmbarkationManager::RemoveEmbarkLoadoutItem(FName ItemId)
{
	FWorldForgeSimpleEmbarkLoadoutOption Option;
	if (!TryResolveStarterEmbarkLoadoutOption(ItemId, Option))
	{
		return false;
	}

	const int32 ExistingQuantity = GetEmbarkLoadoutItemQuantity(ItemId);
	if (ExistingQuantity <= 0)
	{
		return false;
	}

	const int32 NewQuantity = ExistingQuantity - 1;
	if (!SetEmbarkLoadoutItemQuantity(ItemId, NewQuantity))
	{
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkLoadout] Removed %s Qty=%d"),
		*Option.DisplayName.ToString(),
		NewQuantity);
	return true;
}

bool UWorldForgeEmbarkationManager::AddLoadoutItemById(FName ItemId, int32 DeltaQty)
{
	if (DeltaQty <= 0)
	{
		return false;
	}

	return SetEmbarkLoadoutItemQuantity(ItemId, GetEmbarkLoadoutItemQuantity(ItemId) + DeltaQty);
}

bool UWorldForgeEmbarkationManager::RemoveLoadoutItemById(FName ItemId, int32 DeltaQty)
{
	if (DeltaQty <= 0)
	{
		return false;
	}

	const int32 ExistingQuantity = GetEmbarkLoadoutItemQuantity(ItemId);
	if (ExistingQuantity <= 0)
	{
		return false;
	}

	return SetEmbarkLoadoutItemQuantity(ItemId, FMath::Max(0, ExistingQuantity - DeltaQty));
}

bool UWorldForgeEmbarkationManager::SetLoadoutItemQuantity(FName ItemId, int32 Quantity)
{
	return SetEmbarkLoadoutItemQuantity(ItemId, Quantity);
}

bool UWorldForgeEmbarkationManager::ClearLoadoutItem(FName ItemId)
{
	return SetEmbarkLoadoutItemQuantity(ItemId, 0);
}

TArray<FEmbarkSelectedItem> UWorldForgeEmbarkationManager::GetSelectedLoadoutItems() const
{
	TArray<FEmbarkSelectedItem> Result;
	for (const FEmbarkSelectedItem& Item : EmbarkConfig.SelectedLoadoutItems)
	{
		if (Item.Quantity > 0)
		{
			Result.Add(Item);
		}
	}
	return Result;
}

bool UWorldForgeEmbarkationManager::IsEmbarkLoadoutValid(FString& OutReason) const
{
	const int32 UsedPoints = CalculateUsedEmbarkPoints();
	if (UsedPoints > GetMaxEmbarkPoints())
	{
		OutReason = TEXT("Selected loadout exceeds available embark points.");
		return false;
	}

	OutReason.Reset();
	return true;
}

bool UWorldForgeEmbarkationManager::LogEmbarkLoadoutContinue() const
{
	FString ValidationReason;
	if (!IsEmbarkLoadoutValid(ValidationReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Continue blocked: %s"), *ValidationReason);
		return false;
	}

	const int32 UsedPoints = CalculateUsedEmbarkPoints();
	const int32 RemainingPoints = FMath::Max(0, GetMaxEmbarkPoints() - UsedPoints);
	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkLoadout] Continue valid. Used=%d Remaining=%d"),
		UsedPoints,
		RemainingPoints);

	for (const FEmbarkSelectedItem& Item : EmbarkConfig.SelectedLoadoutItems)
	{
		if (Item.Quantity <= 0)
		{
			continue;
		}

		const FString DisplayName = Item.DisplayName.ToString();
		int32 ItemTotalCost = 0;
		if (!Item.ItemId.IsNone())
		{
			ItemTotalCost = Item.Quantity * GetEmbarkLoadoutItemCost(Item.ItemId);
		}
		else
		{
			for (const FWorldForgeSimpleEmbarkLoadoutOption& Option : GetStarterEmbarkLoadoutOptions())
			{
				if (DoesSelectedItemMatchOption(Item, Option))
				{
					ItemTotalCost = Item.Quantity * Option.PointCost;
					break;
				}
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[EmbarkLoadout] Item %s Qty=%d Total=%d"),
			*DisplayName,
			Item.Quantity,
			ItemTotalCost);

		const FString ItemIdString = Item.ItemId.ToString();
		if (ItemIdString.StartsWith(TEXT("Seed.")))
		{
			FString SeedName;
			ItemIdString.Split(TEXT("."), nullptr, &SeedName);
			const FString CropRowName = FString::Printf(TEXT("LP_%s"), *SeedName);
			UE_LOG(LogTemp, Log,
				TEXT("[EmbarkLoadout] Applying seed item %s Qty=%d CropRow=%s"),
				*ItemIdString,
				Item.Quantity,
				*CropRowName);
		}
	}

	return true;
}

const FWorldForgeSimpleEmbarkLoadoutOption* UWorldForgeEmbarkationManager::FindStarterEmbarkLoadoutOption(FName ItemId) const
{
	for (const FWorldForgeSimpleEmbarkLoadoutOption& Option : GetSimpleLoadoutCatalog())
	{
		if (Option.ItemId == ItemId)
		{
			return &Option;
		}
	}

	if (const FWorldForgeSimpleEmbarkLoadoutOption* DynamicOption = DynamicSeedLoadoutOptions.Find(ItemId))
	{
		return DynamicOption;
	}

	// Support dynamic Seed.* items (from DT_LowPoly dropdown)
	const FString ItemIdStr = ItemId.ToString();
	if (ItemIdStr.StartsWith(TEXT("Seed.")))
	{
		// Return nullptr here; caller should use MakeDynamicSeedOption for these
		return nullptr;
	}

	return nullptr;
}

FWorldForgeSimpleEmbarkLoadoutOption UWorldForgeEmbarkationManager::MakeDynamicSeedOption(FName ItemId) const
{
	const FString ItemIdStr = ItemId.ToString();
	FString SeedName;
	ItemIdStr.Split(TEXT("."), nullptr, &SeedName);
	const FString SeedDisplayName = HumanizeSeedIdentifier(SeedName);

	FWorldForgeSimpleEmbarkLoadoutOption Option;
	Option.ItemId = ItemId;
	Option.DisplayName = FText::FromString(FString::Printf(TEXT("%s Seeds"), *SeedDisplayName));
	Option.ItemTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("WorldForge.Item.Seed.%s"), *SeedName)), false);
	Option.PointCost = 2; // Default seed cost
	return Option;
}

bool UWorldForgeEmbarkationManager::TryResolveStarterEmbarkLoadoutOption(FName ItemId, FWorldForgeSimpleEmbarkLoadoutOption& OutOption) const
{
	if (const FWorldForgeSimpleEmbarkLoadoutOption* Option = FindStarterEmbarkLoadoutOption(ItemId))
	{
		OutOption = *Option;
		return true;
	}

	if (ItemId.ToString().StartsWith(TEXT("Seed.")))
	{
		OutOption = MakeDynamicSeedOption(ItemId);
		return true;
	}

	return false;
}

int32 UWorldForgeEmbarkationManager::FindSelectedLoadoutItemIndex(const FWorldForgeSimpleEmbarkLoadoutOption& Option) const
{
	for (int32 Index = 0; Index < EmbarkConfig.SelectedLoadoutItems.Num(); ++Index)
	{
		if (DoesSelectedItemMatchOption(EmbarkConfig.SelectedLoadoutItems[Index], Option))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UWorldForgeEmbarkationManager::FindSelectedLoadoutItemIndexByItemId(FName ItemId) const
{
	if (ItemId.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < EmbarkConfig.SelectedLoadoutItems.Num(); ++Index)
	{
		if (EmbarkConfig.SelectedLoadoutItems[Index].ItemId == ItemId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UWorldForgeEmbarkationManager::CalculateUsedEmbarkPoints() const
{
	int32 UsedPoints = 0;
	const TArray<FWorldForgeSimpleEmbarkLoadoutOption> Options = GetStarterEmbarkLoadoutOptions();

	for (const FEmbarkSelectedItem& Item : EmbarkConfig.SelectedLoadoutItems)
	{
		if (Item.Quantity <= 0)
		{
			continue;
		}

		if (!Item.ItemId.IsNone())
		{
			UsedPoints += Item.Quantity * GetEmbarkLoadoutItemCost(Item.ItemId);
			continue;
		}

		for (const FWorldForgeSimpleEmbarkLoadoutOption& Option : Options)
		{
			if (DoesSelectedItemMatchOption(Item, Option))
			{
				UsedPoints += Item.Quantity * Option.PointCost;
				break;
			}
		}
	}

	return FMath::Max(0, UsedPoints);
}

void UWorldForgeEmbarkationManager::RefreshUsedEmbarkPoints()
{
	EmbarkConfig.UsedEmbarkPoints = CalculateUsedEmbarkPoints();
}

// ---------------------------------------------------------------------------
// Private colonist generation helpers
// ---------------------------------------------------------------------------

bool UWorldForgeEmbarkationManager::IsPlayableRaceTag(FGameplayTag RaceTag) const
{
	if (!RaceTag.IsValid())
	{
		return false;
	}

	for (const FName& TagName : GetPlayableEmbarkRaceTagNames())
	{
		if (RaceTag == FGameplayTag::RequestGameplayTag(TagName, false))
		{
			return true;
		}
	}

	return false;
}

FGameplayTag UWorldForgeEmbarkationManager::ResolveFallbackPlayableRaceTag() const
{
	// Prefer Dwarf explicitly — it is the canonical starter race.
	const FGameplayTag DwarfTag = FGameplayTag::RequestGameplayTag(FName("WorldForge.Race.Dwarf"), false);
	if (DwarfTag.IsValid())
	{
		return DwarfTag;
	}

	// Dwarf tag failed to resolve (tag table not loaded); try the rest of the playable list.
	for (const FName& TagName : GetPlayableEmbarkRaceTagNames())
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
		if (Tag.IsValid())
		{
			return Tag;
		}
	}

	// Last-resort: TAG_Race_Dwarf is guaranteed registered via UE_DEFINE_GAMEPLAY_TAG.
	return TAG_Race_Dwarf;
}

FGameplayTag UWorldForgeEmbarkationManager::ResolveStartingRaceForIndex(int32 Index) const
{
	if (EmbarkConfig.ColonyMode == EWorldForgeColonyMode::SingleRace)
	{
		// Use the player's chosen race only if it is one of the 8 visual-backed playable races.
		if (IsPlayableRaceTag(EmbarkConfig.SelectedRaceTag))
		{
			return EmbarkConfig.SelectedRaceTag;
		}

		// SelectedRaceTag is missing or not a valid playable race — fall back safely.
		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkationManager] ResolveStartingRaceForIndex[%d]: SelectedRaceTag '%s' is not a valid playable race; using fallback."),
			Index,
			*EmbarkConfig.SelectedRaceTag.ToString());
		return ResolveFallbackPlayableRaceTag();
	}

	// Mixed Colony: rotate deterministically through the allowed playable races by index.
	// Uses GetAllowedPlayableRaceTagsForGeneration so AllowedRaceTags filtering is respected
	// and Human is never included (Human is not in the canonical playable list).
	const TArray<FGameplayTag> AllowedTags = GetAllowedPlayableRaceTagsForGeneration();
	if (AllowedTags.Num() == 0)
	{
		return ResolveFallbackPlayableRaceTag();
	}

	return AllowedTags[Index % AllowedTags.Num()];
}

TArray<FGameplayTag> UWorldForgeEmbarkationManager::GetAllowedPlayableRaceTagsForGeneration() const
{
	// If AllowedRaceTags is populated, filter it down to only the 8 visual-backed playable races.
	// IsPlayableRaceTag rejects Human and any tag not in the canonical list — so Human can never
	// slip through here even if a caller accidentally adds it to AllowedRaceTags.
	if (EmbarkConfig.AllowedRaceTags.Num() > 0)
	{
		TArray<FGameplayTag> Filtered;
		for (const FGameplayTag& Tag : EmbarkConfig.AllowedRaceTags)
		{
			if (IsPlayableRaceTag(Tag))
			{
				Filtered.Add(Tag);
			}
		}

		// If filtering left at least one valid race, use it; otherwise fall through to all 8.
		if (Filtered.Num() > 0)
		{
			return Filtered;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkationManager] GetAllowedPlayableRaceTagsForGeneration: AllowedRaceTags contained no valid playable races; falling back to all 8."));
	}

	return GetDefaultPlayableRaceTags();
}

FGameplayTag UWorldForgeEmbarkationManager::ResolveGeneratedColonistRace(FRandomStream& RandomStream) const
{
	// Randomly picks from the filtered allowed playable races using a deterministic stream.
	// Human is never returned — it is absent from the canonical playable list.
	// Full procedural race weighting is not yet implemented; this is a uniform random pick.
	const TArray<FGameplayTag> PlayableTags = GetAllowedPlayableRaceTagsForGeneration();
	if (PlayableTags.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkationManager] ResolveGeneratedColonistRace: no valid playable races available; using fallback."));
		return ResolveFallbackPlayableRaceTag();
	}

	return PlayableTags[RandomStream.RandRange(0, PlayableTags.Num() - 1)];
}

FWorldForgeEmbarkColonist UWorldForgeEmbarkationManager::GenerateStartingColonistProfile(int32 ColonistIndex, int32 RerollSalt) const
{
	// Build a deterministic per-colonist seed from the world seed, index, and reroll salt.
	// Using small primes to avoid overflow while still producing good spread.
	const int32 BaseSeed = EmbarkConfig.ResolvedSeed != 0
		? EmbarkConfig.ResolvedSeed
		: ResolveSeedString(EmbarkConfig.SeedString);
	const int32 CombinedSeed = BaseSeed ^ (ColonistIndex * 7919) ^ (RerollSalt * 104729);
	FRandomStream RNG(CombinedSeed);

	FWorldForgeEmbarkColonist Colonist;

	// -- Race: use 5N-C helper path (handles Single/Mixed and excludes Human) --
	Colonist.RaceTag = ResolveStartingRaceForIndex(ColonistIndex);

	// -- Role: deterministic cycle through 8 core roles by index --
	const TArray<FName>& Roles = GetCoreEmbarkRoles();
	Colonist.RoleId = Roles[ColonistIndex % Roles.Num()];

	// -- Name: race-flavored pool, picked by stream --
	const TArray<FString>& NamePool = GetColonistNamePool(Colonist.RaceTag);
	const int32 NameIndex = RNG.RandRange(0, NamePool.Num() - 1);
	Colonist.Name = NamePool[NameIndex];

	// Sanitize: if the picked name still contains digits (safety net for stale pools),
	// walk forward through the pool to find the first digit-free name.
	{
		bool bHasDigit = false;
		for (const TCHAR Ch : Colonist.Name)
		{
			if (FChar::IsDigit(Ch)) { bHasDigit = true; break; }
		}
		if (bHasDigit)
		{
			bool bSanitized = false;
			for (int32 Offset = 1; Offset < NamePool.Num(); ++Offset)
			{
				const FString& Candidate = NamePool[(NameIndex + Offset) % NamePool.Num()];
				bool bCandidateHasDigit = false;
				for (const TCHAR Ch : Candidate)
				{
					if (FChar::IsDigit(Ch)) { bCandidateHasDigit = true; break; }
				}
				if (!bCandidateHasDigit)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[EmbarkationManager] Colonist %d: name '%s' contained digits — sanitized to '%s' (Race=%s)"),
						ColonistIndex, *Colonist.Name, *Candidate, *GetColonistRaceDisplayName(Colonist.RaceTag));
					Colonist.Name = Candidate;
					bSanitized = true;
					break;
				}
			}
			if (!bSanitized)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[EmbarkationManager] Colonist %d: all names in pool contained digits for Race=%s. Using raw pick."),
					ColonistIndex, *GetColonistRaceDisplayName(Colonist.RaceTag));
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Colonist %d name generated: '%s' (Race=%s)"),
		ColonistIndex, *Colonist.Name, *GetColonistRaceDisplayName(Colonist.RaceTag));

	// -- Age: race-specific range --
	const FColonistAgeRange AgeRange = GetColonistAgeRange(Colonist.RaceTag);
	Colonist.Age = RNG.RandRange(AgeRange.Min, AgeRange.Max);

	// -- Skills: all 8 core keys. Primary 8–15, one random secondary 5–12, rest 1–8 --
	// TODO: Fully data-drive skill keys from DT_WorldForgeSkills row names when struct access is wired.
	const TArray<FName>& CoreSkills = GetCoreEmbarkSkillNames();
	const FName PrimarySkill = GetPrimarySkillForEmbarkRole(Colonist.RoleId);

	FName SecondarySkill = NAME_None;
	{
		TArray<FName> NonPrimary;
		for (const FName& Skill : CoreSkills)
		{
			if (Skill != PrimarySkill) NonPrimary.Add(Skill);
		}
		if (NonPrimary.Num() > 0)
		{
			SecondarySkill = NonPrimary[RNG.RandRange(0, NonPrimary.Num() - 1)];
		}
	}

	for (const FName& SkillName : CoreSkills)
	{
		int32 Value;
		if      (SkillName == PrimarySkill)    Value = RNG.RandRange(8, 15);
		else if (SkillName == SecondarySkill)  Value = RNG.RandRange(5, 12);
		else                                   Value = RNG.RandRange(1,  8);
		Colonist.Skills.Add(SkillName, Value);
	}

	// -- Attributes: base 1–8 + role bias, clamped to [1, 10] --
	int32 StrBias = 0, DexBias = 0, IntBias = 0, EndBias = 0, ChaBias = 0;
	if      (Colonist.RoleId == FName("Warrior"))  { StrBias = 3; EndBias = 3; }
	else if (Colonist.RoleId == FName("Scholar"))  { IntBias = 3; }
	else if (Colonist.RoleId == FName("Healer"))   { IntBias = 2; ChaBias = 3; }
	else if (Colonist.RoleId == FName("Hauler"))   { EndBias = 3; }
	else if (Colonist.RoleId == FName("Builder"))  { StrBias = 2; DexBias = 2; }
	else if (Colonist.RoleId == FName("Miner"))    { StrBias = 2; EndBias = 2; }
	else if (Colonist.RoleId == FName("Farmer"))   { EndBias = 2; }
	else if (Colonist.RoleId == FName("Crafter"))  { DexBias = 2; IntBias = 2; }

	auto RollAttr = [&](int32 Bias) -> int32
	{
		return FMath::Clamp(RNG.RandRange(1, 8) + Bias, 1, 10);
	};

	Colonist.Strength     = RollAttr(StrBias);
	Colonist.Dexterity    = RollAttr(DexBias);
	Colonist.Intelligence = RollAttr(IntBias);
	Colonist.Endurance    = RollAttr(EndBias);
	Colonist.Charisma     = RollAttr(ChaBias);

	// -- Traits: 1–3, no duplicates, from fallback pool --
	// TODO: Data-drive trait pool from DT_WorldForgeTraits when trait row struct access is available.
	const TArray<FString>& TraitPool = GetColonistTraitPool();
	const int32 TraitCount = RNG.RandRange(1, 3);
	TArray<int32> UsedTraitIndices;
	for (int32 T = 0; T < TraitCount; ++T)
	{
		for (int32 Attempt = 0; Attempt < 32; ++Attempt)
		{
			const int32 TIdx = RNG.RandRange(0, TraitPool.Num() - 1);
			if (!UsedTraitIndices.Contains(TIdx))
			{
				UsedTraitIndices.Add(TIdx);
				Colonist.Traits.Add(TraitPool[TIdx]);
				break;
			}
		}
	}

	// -- Backstory: template-driven, 1–2 sentences --
	const FString RaceDisplay   = GetColonistRaceDisplayName(Colonist.RaceTag);
	const FString PrimaryTrait  = Colonist.Traits.Num() > 0 ? Colonist.Traits[0] : TEXT("determined");
	const int32 BackstoryVariant = RNG.RandRange(0, 3);
	Colonist.Backstory = BuildColonistBackstory(
		Colonist.Name, RaceDisplay, Colonist.RoleId.ToString(),
		PrimaryTrait, PrimarySkill.ToString(), BackstoryVariant);

	return Colonist;
}

FString UWorldForgeEmbarkationManager::MakeSafeSaveSlotName(const FString& InWorldName)
{
	const FString TrimmedWorldName = InWorldName.TrimStartAndEnd();
	FString Sanitized;
	Sanitized.Reserve(TrimmedWorldName.Len());

	for (int32 Index = 0; Index < TrimmedWorldName.Len(); ++Index)
	{
		const TCHAR Character = TrimmedWorldName[Index];

		if (FChar::IsWhitespace(Character))
		{
			Sanitized.AppendChar(TCHAR('_'));
			continue;
		}

		if (IsAllowedSaveSlotCharacter(Character))
		{
			Sanitized.AppendChar(Character);
		}
	}

	while (Sanitized.Contains(TEXT("__")))
	{
		Sanitized.ReplaceInline(TEXT("__"), TEXT("_"));
	}

	Sanitized.TrimStartAndEndInline();
	bool bRemoved = false;
	Sanitized.TrimCharInline(TCHAR('_'), &bRemoved);
	Sanitized.TrimCharInline(TCHAR('-'), &bRemoved);

	return ContainsLetterOrNumber(Sanitized) ? Sanitized : FString(DefaultSaveSlotName);
}

int32 UWorldForgeEmbarkationManager::ResolveSeedString(const FString& InSeedString)
{
	const FString TrimmedSeed = InSeedString.TrimStartAndEnd();
	const FString SeedToResolve = TrimmedSeed.IsEmpty() ? TEXT("12345") : TrimmedSeed;
	return static_cast<int32>(GetTypeHash(SeedToResolve));
}

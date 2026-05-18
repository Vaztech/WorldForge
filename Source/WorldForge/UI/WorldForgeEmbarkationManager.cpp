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

	bHasUserEmbarkConfig = false;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Embark config reset to defaults. SeedString='%s' ResolvedSeed=%d WorldSizeGrid=%d SaveSlotName='%s'"),
		*EmbarkConfig.SeedString,
		EmbarkConfig.ResolvedSeed,
		EmbarkConfig.WorldSizeGrid,
		*EmbarkConfig.SaveSlotName);
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
	return TArray<FGameplayTag>{ TAG_Race_Dwarf, TAG_Race_HighElf, TAG_Race_Human };
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

void UWorldForgeEmbarkationManager::ClearColonists()
{
	EmbarkConfig.StartingColonists.Reset();
	EmbarkConfig.StartingColonistCount = 0;

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

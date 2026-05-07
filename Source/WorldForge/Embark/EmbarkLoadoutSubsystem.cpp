// Source/WorldForge/Embark/EmbarkLoadoutSubsystem.cpp

#include "Embark/EmbarkLoadoutSubsystem.h"

#include "Colony/SettlementManager.h"
#include "Engine/World.h"

void UEmbarkLoadoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    RebuildDefaultEmbarkOptions();
    ResetToDefaultStarterLoadout();

    UE_LOG(LogTemp, Log,
        TEXT("[EmbarkLoadout] Initialized. Options=%d CurrentItems=%d MaxPoints=%d"),
        AvailableOptions.Num(),
        CurrentLoadout.Num(),
        MaxEmbarkPoints);
}

FGameplayTag UEmbarkLoadoutSubsystem::MakeItemTag(const TCHAR* TagName) const
{
    // Use bErrorIfNotFound=false so missing experimental tags do not crash/editor-error.
    // Missing tags simply return invalid and are skipped when applying loadouts.
    return FGameplayTag::RequestGameplayTag(FName(TagName), false);
}

void UEmbarkLoadoutSubsystem::AddBuiltInOption(
    const TCHAR* TagName,
    const TCHAR* DisplayName,
    const TCHAR* Description,
    EEmbarkLoadoutCategory Category,
    int32 QuantityPerBundle,
    int32 PointCostPerBundle,
    bool bDebugOnly)
{
    FEmbarkLoadoutOption Option;
    Option.ItemTag = MakeItemTag(TagName);
    Option.DisplayName = FText::FromString(DisplayName);
    Option.Description = FText::FromString(Description);
    Option.Category = Category;
    Option.QuantityPerBundle = FMath::Max(1, QuantityPerBundle);
    Option.PointCostPerBundle = FMath::Max(0, PointCostPerBundle);
    Option.bDebugOnly = bDebugOnly;
    Option.bVisibleInMenu = !bDebugOnly;

    AvailableOptions.Add(Option);

    if (!Option.ItemTag.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[EmbarkLoadout] Option '%s' uses missing GameplayTag '%s'. Add it to DefaultGameplayTags.ini before applying."),
            DisplayName,
            TagName);
    }
}

void UEmbarkLoadoutSubsystem::RebuildDefaultEmbarkOptions()
{
    AvailableOptions.Reset();

    // ------------------------------------------------------------------
    // Basic supplies — existing stockpile resources.
    // ------------------------------------------------------------------
    AddBuiltInOption(TEXT("WorldForge.Item.Food"), TEXT("Food"), TEXT("General starting food."), EEmbarkLoadoutCategory::BasicSupplies, 50, 5);
    AddBuiltInOption(TEXT("WorldForge.Item.Wood"), TEXT("Wood"), TEXT("Basic construction/crafting material."), EEmbarkLoadoutCategory::BasicSupplies, 50, 5);
    AddBuiltInOption(TEXT("WorldForge.Item.Stone"), TEXT("Stone"), TEXT("Basic construction material."), EEmbarkLoadoutCategory::BasicSupplies, 50, 5);

    // ------------------------------------------------------------------
    // Current farming gate tags.
    // These are category-level seeds used by the existing FarmingSubsystem.
    // Keep these until the farming backend switches fully to per-crop seeds.
    // ------------------------------------------------------------------
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Grain"), TEXT("Grain Seeds"), TEXT("Category seed used by grain crops such as Wheat, Barley, Corn, Oat, Rice, and SugarCane."), EEmbarkLoadoutCategory::Seeds, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Berry"), TEXT("Berry Seeds"), TEXT("Category seed used by Blueberry, Cranberry, Currant, Gooseberry, Raspberry, and Strawberry."), EEmbarkLoadoutCategory::Seeds, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Herb"), TEXT("Herb Seeds"), TEXT("Category seed used by Basil, Chamomile, Dill, Echinacea, Lavender, and Sage."), EEmbarkLoadoutCategory::Seeds, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Mushroom"), TEXT("Mushroom Spores"), TEXT("Category seed/spore used by mushroom crops."), EEmbarkLoadoutCategory::Seeds, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.RootVegetable"), TEXT("Root Vegetable Seeds"), TEXT("Category seed used by Carrot, Ginger, Onion, Potato, Sweet Potato, and Turnip."), EEmbarkLoadoutCategory::Seeds, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.SaladGarden"), TEXT("Salad Garden Seeds"), TEXT("Category seed for future leafy/salad crops."), EEmbarkLoadoutCategory::Seeds, 25, 2);

    // ------------------------------------------------------------------
    // Fertilizer.
    // ------------------------------------------------------------------
    AddBuiltInOption(TEXT("WorldForge.Item.Fertilizer.Basic"), TEXT("Basic Fertilizer"), TEXT("Improves crop yield when fertilizer support is enabled."), EEmbarkLoadoutCategory::Fertilizer, 10, 3);

    // ------------------------------------------------------------------
    // Per-crop seeds from DT_LowPoly.
    // Naming convention:
    //   WorldForge.Item.Seed.Potato -> grows potatoes later.
    // These are included now for future UI/data-driven crop selection.
    // ------------------------------------------------------------------
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Barley"), TEXT("Barley Seeds"), TEXT("Seeds for Barley."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Corn"), TEXT("Corn Seeds"), TEXT("Seeds for Corn."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Oat"), TEXT("Oat Seeds"), TEXT("Seeds for Oat."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Rice"), TEXT("Rice Seeds"), TEXT("Seeds for Rice."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.SugarCane"), TEXT("Sugar Cane Starts"), TEXT("Plant starts for Sugar Cane."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Wheat"), TEXT("Wheat Seeds"), TEXT("Seeds for Wheat."), EEmbarkLoadoutCategory::Seeds, 25, 1);

    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Blueberry"), TEXT("Blueberry Seeds"), TEXT("Seeds for Blueberry."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Cranberry"), TEXT("Cranberry Seeds"), TEXT("Seeds for Cranberry."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Currant"), TEXT("Currant Seeds"), TEXT("Seeds for Currant."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Gooseberry"), TEXT("Gooseberry Seeds"), TEXT("Seeds for Gooseberry."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Raspberry"), TEXT("Raspberry Seeds"), TEXT("Seeds for Raspberry."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Strawberry"), TEXT("Strawberry Seeds"), TEXT("Seeds for Strawberry."), EEmbarkLoadoutCategory::Seeds, 25, 1);

    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Basil"), TEXT("Basil Seeds"), TEXT("Seeds for Basil."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Chamomile"), TEXT("Chamomile Seeds"), TEXT("Seeds for Chamomile."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Dill"), TEXT("Dill Seeds"), TEXT("Seeds for Dill."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Echinacea"), TEXT("Echinacea Seeds"), TEXT("Seeds for Echinacea."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Lavender"), TEXT("Lavender Seeds"), TEXT("Seeds for Lavender."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Sage"), TEXT("Sage Seeds"), TEXT("Seeds for Sage."), EEmbarkLoadoutCategory::Seeds, 25, 1);

    AddBuiltInOption(TEXT("WorldForge.Item.Seed.BlackTrumpet"), TEXT("Black Trumpet Spores"), TEXT("Spores for Black Trumpet mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Chanterelle"), TEXT("Chanterelle Spores"), TEXT("Spores for Chanterelle mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Enoki"), TEXT("Enoki Spores"), TEXT("Spores for Enoki mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.FlyAgaric"), TEXT("Fly Agaric Spores"), TEXT("Spores for Fly Agaric mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.KingOyster"), TEXT("King Oyster Spores"), TEXT("Spores for King Oyster mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.TurkeyTail"), TEXT("Turkey Tail Spores"), TEXT("Spores for Turkey Tail mushrooms."), EEmbarkLoadoutCategory::Seeds, 25, 1);

    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Carrot"), TEXT("Carrot Seeds"), TEXT("Seeds for Carrot."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Ginger"), TEXT("Ginger Starts"), TEXT("Plant starts for Ginger."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Onion"), TEXT("Onion Sets"), TEXT("Plant sets for Onion."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Potato"), TEXT("Potato Seeds"), TEXT("Seed potatoes used to grow Potato crops."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.SweetPotato"), TEXT("Sweet Potato Slips"), TEXT("Slips used to grow Sweet Potato crops."), EEmbarkLoadoutCategory::Seeds, 25, 1);
    AddBuiltInOption(TEXT("WorldForge.Item.Seed.Turnip"), TEXT("Turnip Seeds"), TEXT("Seeds for Turnip."), EEmbarkLoadoutCategory::Seeds, 25, 1);

    // ------------------------------------------------------------------
    // Final crop products from DT_LowPoly.
    // These can be used later as food/crafting/trade items.
    // Example requested by user: Potato seed -> Potato as final food/product.
    // ------------------------------------------------------------------
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Barley"), TEXT("Barley"), TEXT("Harvested Barley crop."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Corn"), TEXT("Corn"), TEXT("Harvested Corn crop."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Oat"), TEXT("Oat"), TEXT("Harvested Oat crop."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Rice"), TEXT("Rice"), TEXT("Harvested Rice crop."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.SugarCane"), TEXT("Sugar Cane"), TEXT("Harvested Sugar Cane crop."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Wheat"), TEXT("Wheat"), TEXT("Harvested Wheat crop."), EEmbarkLoadoutCategory::Crops, 25, 2);

    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Blueberry"), TEXT("Blueberry"), TEXT("Harvested Blueberries."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Cranberry"), TEXT("Cranberry"), TEXT("Harvested Cranberries."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Currant"), TEXT("Currant"), TEXT("Harvested Currants."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Gooseberry"), TEXT("Gooseberry"), TEXT("Harvested Gooseberries."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Raspberry"), TEXT("Raspberry"), TEXT("Harvested Raspberries."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Strawberry"), TEXT("Strawberry"), TEXT("Harvested Strawberries."), EEmbarkLoadoutCategory::Crops, 25, 2);

    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Basil"), TEXT("Basil"), TEXT("Harvested Basil."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Chamomile"), TEXT("Chamomile"), TEXT("Harvested Chamomile."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Dill"), TEXT("Dill"), TEXT("Harvested Dill."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Echinacea"), TEXT("Echinacea"), TEXT("Harvested Echinacea."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Lavender"), TEXT("Lavender"), TEXT("Harvested Lavender."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Sage"), TEXT("Sage"), TEXT("Harvested Sage."), EEmbarkLoadoutCategory::Crops, 25, 2);

    AddBuiltInOption(TEXT("WorldForge.Item.Crop.BlackTrumpet"), TEXT("Black Trumpet"), TEXT("Harvested Black Trumpet mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Chanterelle"), TEXT("Chanterelle"), TEXT("Harvested Chanterelle mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Enoki"), TEXT("Enoki"), TEXT("Harvested Enoki mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.FlyAgaric"), TEXT("Fly Agaric"), TEXT("Harvested Fly Agaric mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.KingOyster"), TEXT("King Oyster"), TEXT("Harvested King Oyster mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.TurkeyTail"), TEXT("Turkey Tail"), TEXT("Harvested Turkey Tail mushrooms."), EEmbarkLoadoutCategory::Crops, 25, 2);

    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Carrot"), TEXT("Carrot"), TEXT("Harvested Carrots."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Ginger"), TEXT("Ginger"), TEXT("Harvested Ginger."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Onion"), TEXT("Onion"), TEXT("Harvested Onions."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Potato"), TEXT("Potato"), TEXT("Harvested Potatoes. Food product from Potato Seeds."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.SweetPotato"), TEXT("Sweet Potato"), TEXT("Harvested Sweet Potatoes."), EEmbarkLoadoutCategory::Crops, 25, 2);
    AddBuiltInOption(TEXT("WorldForge.Item.Crop.Turnip"), TEXT("Turnip"), TEXT("Harvested Turnips."), EEmbarkLoadoutCategory::Crops, 25, 2);

    UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Built default option catalog. Options=%d"), AvailableOptions.Num());
}

TArray<FEmbarkLoadoutOption> UEmbarkLoadoutSubsystem::GetEmbarkOptionsByCategory(EEmbarkLoadoutCategory Category, bool bIncludeDebug) const
{
    TArray<FEmbarkLoadoutOption> Result;

    for (const FEmbarkLoadoutOption& Option : AvailableOptions)
    {
        if (Option.Category != Category)
        {
            continue;
        }

        if (Option.bDebugOnly && !bIncludeDebug)
        {
            continue;
        }

        Result.Add(Option);
    }

    return Result;
}

bool UEmbarkLoadoutSubsystem::GetEmbarkOptionByTag(FGameplayTag ItemTag, FEmbarkLoadoutOption& OutOption) const
{
    if (!ItemTag.IsValid())
    {
        return false;
    }

    for (const FEmbarkLoadoutOption& Option : AvailableOptions)
    {
        if (Option.ItemTag == ItemTag)
        {
            OutOption = Option;
            return true;
        }
    }

    return false;
}

void UEmbarkLoadoutSubsystem::AddEmbarkOption(const FEmbarkLoadoutOption& Option)
{
    if (!Option.ItemTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] AddEmbarkOption ignored: invalid ItemTag."));
        return;
    }

    for (FEmbarkLoadoutOption& Existing : AvailableOptions)
    {
        if (Existing.ItemTag == Option.ItemTag)
        {
            Existing = Option;
            return;
        }
    }

    AvailableOptions.Add(Option);
}

void UEmbarkLoadoutSubsystem::ClearCurrentLoadout()
{
    CurrentLoadout.Reset();
}

int32 UEmbarkLoadoutSubsystem::FindCurrentLoadoutIndex(FGameplayTag ItemTag) const
{
    for (int32 Index = 0; Index < CurrentLoadout.Num(); ++Index)
    {
        if (CurrentLoadout[Index].ItemTag == ItemTag)
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

void UEmbarkLoadoutSubsystem::AddOrMergeCurrentItem(FGameplayTag ItemTag, int32 Quantity, const FText& DisplayName)
{
    if (!ItemTag.IsValid() || Quantity <= 0)
    {
        return;
    }

    const int32 ExistingIndex = FindCurrentLoadoutIndex(ItemTag);
    if (ExistingIndex != INDEX_NONE)
    {
        CurrentLoadout[ExistingIndex].Quantity += Quantity;
        return;
    }

    FEmbarkSelectedItem NewItem;
    NewItem.ItemTag = ItemTag;
    NewItem.Quantity = Quantity;
    NewItem.DisplayName = DisplayName;
    CurrentLoadout.Add(NewItem);
}

void UEmbarkLoadoutSubsystem::SetCurrentLoadoutItem(FGameplayTag ItemTag, int32 Quantity)
{
    if (!ItemTag.IsValid())
    {
        return;
    }

    const int32 ExistingIndex = FindCurrentLoadoutIndex(ItemTag);

    if (Quantity <= 0)
    {
        if (ExistingIndex != INDEX_NONE)
        {
            CurrentLoadout.RemoveAt(ExistingIndex);
        }
        return;
    }

    FEmbarkLoadoutOption Option;
    const bool bHasOption = GetEmbarkOptionByTag(ItemTag, Option);

    if (ExistingIndex != INDEX_NONE)
    {
        CurrentLoadout[ExistingIndex].Quantity = Quantity;
        CurrentLoadout[ExistingIndex].DisplayName = bHasOption ? Option.DisplayName : FText::FromString(ItemTag.ToString());
        return;
    }

    FEmbarkSelectedItem NewItem;
    NewItem.ItemTag = ItemTag;
    NewItem.Quantity = Quantity;
    NewItem.DisplayName = bHasOption ? Option.DisplayName : FText::FromString(ItemTag.ToString());
    CurrentLoadout.Add(NewItem);
}

void UEmbarkLoadoutSubsystem::AddItemToCurrentLoadout(FGameplayTag ItemTag, int32 Quantity)
{
    if (!ItemTag.IsValid() || Quantity <= 0)
    {
        return;
    }

    const int32 ExistingIndex = FindCurrentLoadoutIndex(ItemTag);
    const int32 ExistingQuantity = ExistingIndex != INDEX_NONE ? CurrentLoadout[ExistingIndex].Quantity : 0;
    SetCurrentLoadoutItem(ItemTag, ExistingQuantity + Quantity);
}

void UEmbarkLoadoutSubsystem::RemoveItemFromCurrentLoadout(FGameplayTag ItemTag, int32 Quantity)
{
    if (!ItemTag.IsValid() || Quantity <= 0)
    {
        return;
    }

    const int32 ExistingIndex = FindCurrentLoadoutIndex(ItemTag);
    if (ExistingIndex == INDEX_NONE)
    {
        return;
    }

    SetCurrentLoadoutItem(ItemTag, CurrentLoadout[ExistingIndex].Quantity - Quantity);
}

int32 UEmbarkLoadoutSubsystem::GetUsedEmbarkPoints() const
{
    int32 Total = 0;

    for (const FEmbarkSelectedItem& Item : CurrentLoadout)
    {
        FEmbarkLoadoutOption Option;
        if (!GetEmbarkOptionByTag(Item.ItemTag, Option))
        {
            continue;
        }

        const int32 BundleSize = FMath::Max(1, Option.QuantityPerBundle);
        const int32 Bundles = FMath::DivideAndRoundUp(FMath::Max(0, Item.Quantity), BundleSize);
        Total += Bundles * Option.PointCostPerBundle;
    }

    return Total;
}

void UEmbarkLoadoutSubsystem::ResetToDefaultStarterLoadout()
{
    ClearCurrentLoadout();

    // Existing general resources.
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Food")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Wood")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Stone")), 100);

    // Important: current FarmingSubsystem gates planting on category seed tags.
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Grain")), 50);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.RootVegetable")), 25);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Berry")), 25);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Herb")), 25);

    // Useful once fertilizer path is enabled.
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Fertilizer.Basic")), 10);

    UE_LOG(LogTemp, Log,
        TEXT("[EmbarkLoadout] Reset to default starter loadout. Items=%d UsedPoints=%d/%d"),
        CurrentLoadout.Num(),
        GetUsedEmbarkPoints(),
        MaxEmbarkPoints);
}

void UEmbarkLoadoutSubsystem::ResetToFarmTestingLoadout()
{
    ClearCurrentLoadout();

    // Category seed tags used by current planting gate.
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Grain")), 200);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Berry")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Herb")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Mushroom")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.RootVegetable")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.SaladGarden")), 100);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Fertilizer.Basic")), 50);

    // Per-crop examples requested by user / future UI.
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Potato")), 50);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Crop.Potato")), 50);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Seed.Wheat")), 50);
    SetCurrentLoadoutItem(MakeItemTag(TEXT("WorldForge.Item.Crop.Wheat")), 50);

    UE_LOG(LogTemp, Warning,
        TEXT("[EmbarkLoadout] Reset to FARM TESTING loadout. Items=%d UsedPoints=%d/%d"),
        CurrentLoadout.Num(),
        GetUsedEmbarkPoints(),
        MaxEmbarkPoints);
}

USettlementManager* UEmbarkLoadoutSubsystem::GetSettlementManager() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<USettlementManager>() : nullptr;
}

bool UEmbarkLoadoutSubsystem::ApplyCurrentLoadoutToSettlement(int32 SettlementID)
{
    return ApplyLoadoutToSettlement(SettlementID, CurrentLoadout);
}

bool UEmbarkLoadoutSubsystem::ApplyDebugStarterLoadout(int32 SettlementID)
{
    ResetToDefaultStarterLoadout();
    return ApplyCurrentLoadoutToSettlement(SettlementID);
}

bool UEmbarkLoadoutSubsystem::ApplyLoadoutToSettlement(int32 SettlementID, const TArray<FEmbarkSelectedItem>& LoadoutItems)
{
    USettlementManager* SettlementManager = GetSettlementManager();
    if (!SettlementManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Apply failed: SettlementManager missing."));
        return false;
    }

    if (!SettlementManager->SettlementExists(SettlementID))
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Apply failed: Settlement %d does not exist."), SettlementID);
        return false;
    }

    int32 AppliedCount = 0;
    int32 SkippedCount = 0;

    for (const FEmbarkSelectedItem& Item : LoadoutItems)
    {
        if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
        {
            ++SkippedCount;
            continue;
        }

        if (SettlementManager->AddToStockpile(SettlementID, Item.ItemTag, Item.Quantity))
        {
            ++AppliedCount;
            UE_LOG(LogTemp, Log,
                TEXT("[EmbarkLoadout] Added %d of %s to settlement %d."),
                Item.Quantity,
                *Item.ItemTag.ToString(),
                SettlementID);
        }
        else
        {
            ++SkippedCount;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[EmbarkLoadout] Applied loadout to settlement %d. Applied=%d Skipped=%d"),
        SettlementID,
        AppliedCount,
        SkippedCount);

    return AppliedCount > 0;
}

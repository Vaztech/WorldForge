#include "UI/WorldForgeEmbarkLoadoutWidget.h"

#include "UI/WorldForgeEmbarkationManager.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SizeBox.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/UnrealType.h"

namespace
{
	const FName ItemFoodRations(TEXT("FoodRations"));
	const FName ItemWood(TEXT("Wood"));
	const FName ItemStone(TEXT("Stone"));
	const FName ItemSeeds(TEXT("Seeds"));
	const FName ItemPickaxe(TEXT("Pickaxe"));
	const FName ItemMedicine(TEXT("Medicine"));
	const FName ItemCloth(TEXT("Cloth"));
	const FName ItemCopperIngots(TEXT("CopperIngots"));


	enum ESelectedLoadoutSummaryAction : int32
	{
		SummaryActionMinus = 0,
		SummaryActionPlus = 1,
		SummaryActionRemove = 2
	};

	FString SanitizeWidgetNameFromItemId(FName ItemId)
	{
		const FString Source = ItemId.ToString();
		FString Result;
		Result.Reserve(Source.Len());

		for (int32 Index = 0; Index < Source.Len(); ++Index)
		{
			const TCHAR Character = Source[Index];
			Result.AppendChar(FChar::IsAlnum(Character) ? Character : TCHAR('_'));
		}

		return Result.IsEmpty() ? TEXT("Unknown") : Result;
	}

	UTextBlock* ConstructSummaryText(UWidgetTree* WidgetTree, const FString& Name, const FString& Text)
	{
		UTextBlock* TextBlock = WidgetTree ? WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name)) : nullptr;
		if (TextBlock)
		{
			TextBlock->SetText(FText::FromString(Text));

			FSlateFontInfo FontInfo = TextBlock->GetFont();
			FontInfo.Size = 10;
			TextBlock->SetFont(FontInfo);
			TextBlock->SetClipping(EWidgetClipping::ClipToBounds);
		}
		return TextBlock;
	}

	UButton* ConstructSummaryButton(UWidgetTree* WidgetTree, const FString& Name, const FString& Label)
	{
		UButton* Button = WidgetTree ? WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name)) : nullptr;
		if (Button)
		{
			if (UTextBlock* LabelText = ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_%s"), *Name), Label))
			{
				Button->AddChild(LabelText);
			}
		}
		return Button;
	}
	const TCHAR* DT_LowPolyPath = TEXT("/Game/WildHarvest/WH_LowPoly/Blueprints/DT_LowPoly.DT_LowPoly");

	const TCHAR* BindingStatus(const UObject* Widget)
	{
		return Widget ? TEXT("OK") : TEXT("NULL");
	}

	void SetTextBlock(UTextBlock* TextBlock, const FString& Text)
	{
		if (TextBlock)
		{
			TextBlock->SetText(FText::FromString(Text));
		}
	}

	FString NormalizeSeedTypeDisplayName(const FString& Input)
	{
		FString Result = Input.TrimStartAndEnd();
		if (Result.EndsWith(TEXT(" Seeds"), ESearchCase::IgnoreCase))
		{
			Result.LeftChopInline(6);
			Result = Result.TrimStartAndEnd();
		}
		else if (Result.EndsWith(TEXT(" Seed"), ESearchCase::IgnoreCase))
		{
			Result.LeftChopInline(5);
			Result = Result.TrimStartAndEnd();
		}

		return Result;
	}

	FString SanitizeSeedName(const FString& Input)
	{
		const FString Base = NormalizeSeedTypeDisplayName(Input);
		const FString CanonicalLookup = Base.Replace(TEXT(" "), TEXT("")).ToLower();
		if (CanonicalLookup == TEXT("rice"))
		{
			return TEXT("Rice");
		}
		if (CanonicalLookup == TEXT("corn"))
		{
			return TEXT("Corn");
		}
		if (CanonicalLookup == TEXT("wheat"))
		{
			return TEXT("Wheat");
		}
		if (CanonicalLookup == TEXT("barley"))
		{
			return TEXT("Barley");
		}

		FString Result;
		Result.Reserve(Base.Len());
		for (int32 Index = 0; Index < Base.Len(); ++Index)
		{
			const TCHAR Ch = Base[Index];
			if (FChar::IsAlnum(Ch))
			{
				Result.AppendChar(Ch);
			}
		}
		return Result;
	}

	FString NormalizePropertyLookupName(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len());

		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			const TCHAR Character = Input[Index];
			if (FChar::IsAlnum(Character))
			{
				Result.AppendChar(FChar::ToLower(Character));
			}
		}

		return Result;
	}

	FString ReadRowStringProperty(const UScriptStruct* RowStruct, const uint8* RowData, const TArray<FString>& CandidateNames)
	{
		if (!RowStruct || !RowData)
		{
			return FString();
		}

		TArray<FString> NormalizedCandidateNames;
		NormalizedCandidateNames.Reserve(CandidateNames.Num());
		for (const FString& CandidateName : CandidateNames)
		{
			NormalizedCandidateNames.Add(NormalizePropertyLookupName(CandidateName));
		}

		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			const FProperty* Property = *It;
			const FString PropertyName = NormalizePropertyLookupName(Property->GetName());
			const FString DisplayName = NormalizePropertyLookupName(Property->GetDisplayNameText().ToString());

			if (!NormalizedCandidateNames.Contains(PropertyName) && !NormalizedCandidateNames.Contains(DisplayName))
			{
				continue;
			}

			if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
			{
				return StrProperty->GetPropertyValue_InContainer(RowData).TrimStartAndEnd();
			}

			if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				return TextProperty->GetPropertyValue_InContainer(RowData).ToString().TrimStartAndEnd();
			}

			if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				return NameProperty->GetPropertyValue_InContainer(RowData).ToString().TrimStartAndEnd();
			}
		}

		return FString();
	}

	FText MakeSeedItemDisplayName(const FString& SeedDisplayName)
	{
		const FString SeedName = NormalizeSeedTypeDisplayName(SeedDisplayName);
		return FText::FromString(FString::Printf(TEXT("%s Seeds"), *SeedName));
	}

	FString MakeDisplayNameFromRowName(const FString& RowName)
	{
		// Strip LP_ prefix
		FString Base = RowName;
		if (Base.StartsWith(TEXT("LP_")))
		{
			Base = Base.RightChop(3);
		}

		// Insert spaces before uppercase letters (e.g., SugarCane -> Sugar Cane)
		FString Result;
		Result.Reserve(Base.Len() + 4);
		for (int32 Index = 0; Index < Base.Len(); ++Index)
		{
			const TCHAR Ch = Base[Index];
			if (Index > 0 && FChar::IsUpper(Ch) && FChar::IsLower(Base[Index - 1]))
			{
				Result.AppendChar(TCHAR(' '));
			}
			Result.AppendChar(Ch);
		}
		return Result;
	}
}


void UWorldForgeEmbarkLoadoutSummaryAction::Initialize(UWorldForgeEmbarkLoadoutWidget* InOwner, FName InItemId, int32 InAction)
{
	Owner = InOwner;
	ItemId = InItemId;
	Action = InAction;
}

void UWorldForgeEmbarkLoadoutSummaryAction::HandleClicked()
{
	if (Owner)
	{
		Owner->HandleSummaryAction(ItemId, Action);
	}
}

void UWorldForgeEmbarkLoadoutWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyRuntimeLayoutPolish();
	LogBindingStatus();
	PopulateSeedTypeDropdown();
	BindControls();
	UpdateLoadoutDisplay();
}

void UWorldForgeEmbarkLoadoutWidget::SetSetupWidgetToRestore(UUserWidget* InWidget)
{
	SetupWidgetToRestore = InWidget;
}

void UWorldForgeEmbarkLoadoutWidget::ApplyRuntimeLayoutPolish()
{
	auto SetHorizontalFill = [](UWidget* Widget, float FillValue, const FMargin& EntryPadding)
		{
			if (!Widget)
			{
				return;
			}

			if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
			{
				FSlateChildSize SlotSize;
				SlotSize.SizeRule = ESlateSizeRule::Fill;
				SlotSize.Value = FillValue;
				HorizontalSlot->SetSize(SlotSize);
				HorizontalSlot->SetPadding(EntryPadding);
				HorizontalSlot->SetHorizontalAlignment(HAlign_Fill);
				HorizontalSlot->SetVerticalAlignment(VAlign_Fill);
			}
		};

	SetHorizontalFill(PNL_LoadoutCategories, 0.16f, FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	SetHorizontalFill(PNL_LoadoutItems, 0.52f, FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	SetHorizontalFill(PNL_LoadoutSummary, 0.32f, FMargin(0.0f));

	auto TightenCategoryButton = [](UButton* Button)
		{
			if (!Button)
			{
				return;
			}

			if (UVerticalBoxSlot* CategoryButtonSlot = Cast<UVerticalBoxSlot>(Button->Slot))
			{
				CategoryButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
				CategoryButtonSlot->SetHorizontalAlignment(HAlign_Fill);
			}
		};

	TightenCategoryButton(BTN_CategoryFood);
	TightenCategoryButton(BTN_CategoryTools);
	TightenCategoryButton(BTN_CategoryMaterials);
	TightenCategoryButton(BTN_CategorySeeds);
	TightenCategoryButton(BTN_CategoryMedicine);
	TightenCategoryButton(BTN_CategoryLivestock);
	TightenCategoryButton(BTN_CategoryWeapons);
	TightenCategoryButton(BTN_CategorySpecial);
}

void UWorldForgeEmbarkLoadoutWidget::BindControls()
{
#define BIND_LOADOUT_BUTTON(ButtonName, HandlerName) \
	if (ButtonName) \
	{ \
		ButtonName->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkLoadoutWidget::HandlerName); \
		ButtonName->OnClicked.AddDynamic(this, &UWorldForgeEmbarkLoadoutWidget::HandlerName); \
	}

	BIND_LOADOUT_BUTTON(BTN_FoodRations_Minus, HandleFoodRationsMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_FoodRations_Plus, HandleFoodRationsPlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Wood_Minus, HandleWoodMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Wood_Plus, HandleWoodPlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Stone_Minus, HandleStoneMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Stone_Plus, HandleStonePlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Seeds_Minus, HandleSeedsMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Seeds_Plus, HandleSeedsPlusClicked);

	if (SeedTypeDropdown)
	{
		SeedTypeDropdown->OnSelectionChanged.RemoveDynamic(this, &UWorldForgeEmbarkLoadoutWidget::HandleSeedTypeSelectionChanged);
		SeedTypeDropdown->OnSelectionChanged.AddDynamic(this, &UWorldForgeEmbarkLoadoutWidget::HandleSeedTypeSelectionChanged);
	}
	BIND_LOADOUT_BUTTON(BTN_Pickaxe_Minus, HandlePickaxeMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Pickaxe_Plus, HandlePickaxePlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Medicine_Minus, HandleMedicineMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Medicine_Plus, HandleMedicinePlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Cloth_Minus, HandleClothMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_Cloth_Plus, HandleClothPlusClicked);
	BIND_LOADOUT_BUTTON(BTN_CopperIngots_Minus, HandleCopperIngotsMinusClicked);
	BIND_LOADOUT_BUTTON(BTN_CopperIngots_Plus, HandleCopperIngotsPlusClicked);
	BIND_LOADOUT_BUTTON(BTN_Back, HandleBackClicked);
	BIND_LOADOUT_BUTTON(BTN_Continue, HandleContinueClicked);

#undef BIND_LOADOUT_BUTTON
}

void UWorldForgeEmbarkLoadoutWidget::LogBindingStatus() const
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryFood=%s"), BindingStatus(BTN_CategoryFood));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryTools=%s"), BindingStatus(BTN_CategoryTools));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryMaterials=%s"), BindingStatus(BTN_CategoryMaterials));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategorySeeds=%s"), BindingStatus(BTN_CategorySeeds));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryMedicine=%s"), BindingStatus(BTN_CategoryMedicine));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryLivestock=%s"), BindingStatus(BTN_CategoryLivestock));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategoryWeapons=%s"), BindingStatus(BTN_CategoryWeapons));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_CategorySpecial=%s"), BindingStatus(BTN_CategorySpecial));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] SeedTypeDropdown=%s"), BindingStatus(SeedTypeDropdown));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_Seeds_Minus=%s"), BindingStatus(BTN_Seeds_Minus));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_Seeds_Plus=%s"), BindingStatus(BTN_Seeds_Plus));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_Back=%s"), BindingStatus(BTN_Back));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] BTN_Continue=%s"), BindingStatus(BTN_Continue));
}

UWorldForgeEmbarkationManager* UWorldForgeEmbarkLoadoutWidget::GetEmbarkationManager() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UWorldForgeEmbarkationManager>() : nullptr;
}

void UWorldForgeEmbarkLoadoutWidget::AddItem(FName ItemId)
{
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UpdateLoadoutDisplay(FText::FromString(TEXT("Embark loadout manager unavailable.")));
		return;
	}

	const bool bAdded = Manager->AddLoadoutItemById(ItemId, 1);
	UpdateLoadoutDisplay(bAdded ? FText::GetEmpty() : FText::FromString(TEXT("Not enough embark points.")));
}

void UWorldForgeEmbarkLoadoutWidget::RemoveItem(FName ItemId)
{
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UpdateLoadoutDisplay(FText::FromString(TEXT("Embark loadout manager unavailable.")));
		return;
	}

	const bool bRemoved = Manager->RemoveLoadoutItemById(ItemId, 1);
	UpdateLoadoutDisplay(bRemoved ? FText::GetEmpty() : FText::FromString(TEXT("Quantity cannot go below zero.")));
}

void UWorldForgeEmbarkLoadoutWidget::UpdateLoadoutDisplay(const FText& WarningText)
{
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		SetTextBlock(TXT_EmbarkPoints, TEXT("Embark Points: 100"));
		SetTextBlock(TXT_UsedPoints, TEXT("Used: 0"));
		SetTextBlock(TXT_RemainingPoints, TEXT("Remaining: 100"));
		SetTextBlock(TXT_SelectedItemSummary, TEXT("No items selected."));
		SetTextBlock(TXT_LoadoutWarning, TEXT("Embark loadout manager unavailable."));
		return;
	}

	UpdateItemText(ItemFoodRations, TXT_FoodRations_Qty, TXT_FoodRations_Cost, TXT_FoodRations_Total);
	UpdateItemText(ItemWood, TXT_Wood_Qty, TXT_Wood_Cost, TXT_Wood_Total);
	UpdateItemText(ItemStone, TXT_Stone_Qty, TXT_Stone_Cost, TXT_Stone_Total);
	UpdateItemText(GetActiveSeedItemId(), TXT_Seeds_Qty, TXT_Seeds_Cost, TXT_Seeds_Total);
	UpdateItemText(ItemPickaxe, TXT_Pickaxe_Qty, TXT_Pickaxe_Cost, TXT_Pickaxe_Total);
	UpdateItemText(ItemMedicine, TXT_Medicine_Qty, TXT_Medicine_Cost, TXT_Medicine_Total);
	UpdateItemText(ItemCloth, TXT_Cloth_Qty, TXT_Cloth_Cost, TXT_Cloth_Total);
	UpdateItemText(ItemCopperIngots, TXT_CopperIngots_Qty, TXT_CopperIngots_Cost, TXT_CopperIngots_Total);

	SetTextBlock(TXT_EmbarkPoints, FString::Printf(TEXT("Embark Points: %d"), Manager->GetMaxEmbarkPoints()));
	SetTextBlock(TXT_UsedPoints, FString::Printf(TEXT("Used: %d"), Manager->GetUsedEmbarkPoints()));
	SetTextBlock(TXT_RemainingPoints, FString::Printf(TEXT("Remaining: %d"), Manager->GetRemainingEmbarkPoints()));

	TArray<FString> SelectedLines;
	const FWorldForgeEmbarkConfig CurrentConfig = Manager->GetEmbarkConfig();
	for (const FEmbarkSelectedItem& Item : CurrentConfig.SelectedLoadoutItems)
	{
		if (Item.Quantity <= 0)
		{
			continue;
		}

		const FName ItemId = Item.ItemId;
		const FString CachedDisplayName = Item.DisplayName.ToString();
		const FText DisplayName = CachedDisplayName.IsEmpty()
			? Manager->GetEmbarkLoadoutItemDisplayName(ItemId)
			: Item.DisplayName;

		SelectedLines.Add(FString::Printf(
			TEXT("%s x%d = %d"),
			*DisplayName.ToString(),
			Item.Quantity,
			Manager->GetEmbarkLoadoutItemTotalCost(ItemId)));
	}

	SetTextBlock(TXT_SelectedItemSummary, SelectedLines.Num() > 0 ? FString::Join(SelectedLines, TEXT("\n")) : FString(TEXT("No items selected.")));
	RebuildSelectedLoadoutSummary();
	SetTextBlock(TXT_LoadoutWarning, WarningText.ToString());
}

void UWorldForgeEmbarkLoadoutWidget::EnsureSelectedLoadoutSummaryPanel()
{
	if (SelectedLoadoutItemsPanel || !WidgetTree || !VB_LoadoutSummary)
	{
		return;
	}

	if (TXT_SelectedItemSummary)
	{
		TXT_SelectedItemSummary->SetVisibility(ESlateVisibility::Collapsed);
	}

	SelectedLoadoutItemsScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(TEXT("PNL_SelectedLoadoutItems")));
	SelectedLoadoutItemsPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(TEXT("PNL_SelectedLoadoutItems_List")));

	if (SelectedLoadoutItemsScrollBox && SelectedLoadoutItemsPanel)
	{
		SelectedLoadoutItemsScrollBox->SetClipping(EWidgetClipping::ClipToBounds);
		SelectedLoadoutItemsScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
		SelectedLoadoutItemsScrollBox->AddChild(SelectedLoadoutItemsPanel);
		SelectedLoadoutItemsPanel->SetClipping(EWidgetClipping::ClipToBounds);

		USizeBox* SizeBoxWrapper = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(TEXT("SB_SelectedLoadoutItems")));
		if (SizeBoxWrapper)
		{
			SizeBoxWrapper->SetMinDesiredHeight(80.0f);
			SizeBoxWrapper->SetMaxDesiredHeight(300.0f);
			SizeBoxWrapper->SetClipping(EWidgetClipping::ClipToBounds);
			SizeBoxWrapper->AddChild(SelectedLoadoutItemsScrollBox);

			if (UVerticalBoxSlot* SummarySlot = VB_LoadoutSummary->AddChildToVerticalBox(SizeBoxWrapper))
			{
				FSlateChildSize SlotSize;
				SlotSize.SizeRule = ESlateSizeRule::Fill;
				SlotSize.Value = 1.0f;
				SummarySlot->SetSize(SlotSize);
				SummarySlot->SetHorizontalAlignment(HAlign_Fill);
				SummarySlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
		else if (UVerticalBoxSlot* SummarySlot = VB_LoadoutSummary->AddChildToVerticalBox(SelectedLoadoutItemsScrollBox))
		{
			FSlateChildSize SlotSize;
			SlotSize.SizeRule = ESlateSizeRule::Fill;
			SlotSize.Value = 1.0f;
			SummarySlot->SetSize(SlotSize);
			SummarySlot->SetHorizontalAlignment(HAlign_Fill);
			SummarySlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UWorldForgeEmbarkLoadoutWidget::RebuildSelectedLoadoutSummary()
{
	EnsureSelectedLoadoutSummaryPanel();

	if (!SelectedLoadoutItemsPanel)
	{
		return;
	}

	SelectedLoadoutItemsPanel->ClearChildren();
	SummaryActionHandlers.Reset();
	++SummaryRebuildSerial;

	const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		if (UTextBlock* EmptyText = ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_SelectedItemSummary_ManagerMissing_%d"), SummaryRebuildSerial), TEXT("Embark loadout manager unavailable.")))
		{
			SelectedLoadoutItemsPanel->AddChildToVerticalBox(EmptyText);
		}
		return;
	}

	const TArray<FEmbarkSelectedItem> SelectedItems = Manager->GetSelectedLoadoutItems();
	int32 VisibleCount = 0;
	for (const FEmbarkSelectedItem& Item : SelectedItems)
	{
		if (Item.Quantity <= 0)
		{
			continue;
		}

		const FString CachedDisplayName = Item.DisplayName.ToString();
		const FText DisplayName = CachedDisplayName.IsEmpty()
			? Manager->GetEmbarkLoadoutItemDisplayName(Item.ItemId)
			: Item.DisplayName;
		AddSelectedLoadoutSummaryRow(Item, DisplayName, Manager->GetEmbarkLoadoutItemTotalCost(Item.ItemId));
		++VisibleCount;
	}

	if (VisibleCount == 0)
	{
		if (UTextBlock* EmptyText = ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_SelectedItemSummary_Empty_%d"), SummaryRebuildSerial), TEXT("No items selected.")))
		{
			SelectedLoadoutItemsPanel->AddChildToVerticalBox(EmptyText);
		}
	}
}

void UWorldForgeEmbarkLoadoutWidget::AddSelectedLoadoutSummaryRow(const FEmbarkSelectedItem& Item, const FText& DisplayName, int32 TotalCost)
{
	if (!WidgetTree || !SelectedLoadoutItemsPanel)
	{
		return;
	}

	const FString SanitizedItemId = FString::Printf(TEXT("%s_%d"), *SanitizeWidgetNameFromItemId(Item.ItemId), SummaryRebuildSerial);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("ROW_SelectedItem_%s"), *SanitizedItemId)));
	if (!Row)
	{
		return;
	}

	Row->AddChildToHorizontalBox(ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_SelectedItemName_%s"), *SanitizedItemId), DisplayName.ToString()));
	Row->AddChildToHorizontalBox(ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_SelectedItemQty_%s"), *SanitizedItemId), FString::Printf(TEXT(" x%d "), Item.Quantity)));
	Row->AddChildToHorizontalBox(ConstructSummaryText(WidgetTree, FString::Printf(TEXT("TXT_SelectedItemTotal_%s"), *SanitizedItemId), FString::Printf(TEXT(" %d pts "), TotalCost)));

	auto AddActionButton = [this, &Row, &SanitizedItemId, ItemId = Item.ItemId](const TCHAR* Prefix, const FString& Label, int32 ActionType)
		{
			UButton* Button = ConstructSummaryButton(WidgetTree, FString::Printf(TEXT("%s_%s"), Prefix, *SanitizedItemId), Label);
			if (!Button)
			{
				return;
			}

			UWorldForgeEmbarkLoadoutSummaryAction* Handler = NewObject<UWorldForgeEmbarkLoadoutSummaryAction>(this);
			Handler->Initialize(this, ItemId, ActionType);
			Button->OnClicked.AddDynamic(Handler, &UWorldForgeEmbarkLoadoutSummaryAction::HandleClicked);
			SummaryActionHandlers.Add(Handler);
			Row->AddChildToHorizontalBox(Button);
		};

	AddActionButton(TEXT("BTN_SelectedItemMinus"), TEXT("-"), SummaryActionMinus);
	AddActionButton(TEXT("BTN_SelectedItemPlus"), TEXT("+"), SummaryActionPlus);
	AddActionButton(TEXT("BTN_SelectedItemRemove"), TEXT("Clear"), SummaryActionRemove);

	SelectedLoadoutItemsPanel->AddChildToVerticalBox(Row);
}

void UWorldForgeEmbarkLoadoutWidget::HandleSummaryAction(FName ItemId, int32 Action)
{
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UpdateLoadoutDisplay(FText::FromString(TEXT("Embark loadout manager unavailable.")));
		return;
	}

	const FText DisplayName = Manager->GetEmbarkLoadoutItemDisplayName(ItemId);
	bool bChanged = false;
	FText WarningText = FText::GetEmpty();

	switch (Action)
	{
	case SummaryActionPlus:
		bChanged = Manager->AddLoadoutItemById(ItemId, 1);
		if (bChanged)
		{
			UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Summary plus: %s Qty=%d"), *DisplayName.ToString(), Manager->GetEmbarkLoadoutItemQuantity(ItemId));
		}
		else
		{
			WarningText = FText::FromString(TEXT("Not enough embark points."));
		}
		break;

	case SummaryActionMinus:
		bChanged = Manager->RemoveLoadoutItemById(ItemId, 1);
		if (bChanged)
		{
			UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Summary minus: %s Qty=%d"), *DisplayName.ToString(), Manager->GetEmbarkLoadoutItemQuantity(ItemId));
		}
		else
		{
			WarningText = FText::FromString(TEXT("Quantity cannot go below zero."));
		}
		break;

	case SummaryActionRemove:
		bChanged = Manager->ClearLoadoutItem(ItemId);
		if (bChanged)
		{
			UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Summary removed: %s"), *DisplayName.ToString());
		}
		break;

	default:
		WarningText = FText::FromString(TEXT("Unknown summary action."));
		break;
	}

	UpdateLoadoutDisplay(WarningText);
}

void UWorldForgeEmbarkLoadoutWidget::UpdateItemText(FName ItemId, UTextBlock* QuantityText, UTextBlock* CostText, UTextBlock* TotalText) const
{
	const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		SetTextBlock(QuantityText, TEXT("0"));
		SetTextBlock(CostText, TEXT("0"));
		SetTextBlock(TotalText, TEXT("0"));
		return;
	}

	SetTextBlock(QuantityText, FString::FromInt(Manager->GetEmbarkLoadoutItemQuantity(ItemId)));
	SetTextBlock(CostText, FString::FromInt(Manager->GetEmbarkLoadoutItemCost(ItemId)));
	SetTextBlock(TotalText, FString::FromInt(Manager->GetEmbarkLoadoutItemTotalCost(ItemId)));
}

void UWorldForgeEmbarkLoadoutWidget::HandleFoodRationsMinusClicked()
{
	RemoveItem(ItemFoodRations);
}

void UWorldForgeEmbarkLoadoutWidget::HandleFoodRationsPlusClicked()
{
	AddItem(ItemFoodRations);
}

void UWorldForgeEmbarkLoadoutWidget::HandleWoodMinusClicked()
{
	RemoveItem(ItemWood);
}

void UWorldForgeEmbarkLoadoutWidget::HandleWoodPlusClicked()
{
	AddItem(ItemWood);
}

void UWorldForgeEmbarkLoadoutWidget::HandleStoneMinusClicked()
{
	RemoveItem(ItemStone);
}

void UWorldForgeEmbarkLoadoutWidget::HandleStonePlusClicked()
{
	AddItem(ItemStone);
}

void UWorldForgeEmbarkLoadoutWidget::HandleSeedsMinusClicked()
{
	RemoveItem(GetActiveSeedItemId());
}

void UWorldForgeEmbarkLoadoutWidget::HandleSeedsPlusClicked()
{
	AddItem(GetActiveSeedItemId());
}

void UWorldForgeEmbarkLoadoutWidget::HandlePickaxeMinusClicked()
{
	RemoveItem(ItemPickaxe);
}

void UWorldForgeEmbarkLoadoutWidget::HandlePickaxePlusClicked()
{
	AddItem(ItemPickaxe);
}

void UWorldForgeEmbarkLoadoutWidget::HandleMedicineMinusClicked()
{
	RemoveItem(ItemMedicine);
}

void UWorldForgeEmbarkLoadoutWidget::HandleMedicinePlusClicked()
{
	AddItem(ItemMedicine);
}

void UWorldForgeEmbarkLoadoutWidget::HandleClothMinusClicked()
{
	RemoveItem(ItemCloth);
}

void UWorldForgeEmbarkLoadoutWidget::HandleClothPlusClicked()
{
	AddItem(ItemCloth);
}

void UWorldForgeEmbarkLoadoutWidget::HandleCopperIngotsMinusClicked()
{
	RemoveItem(ItemCopperIngots);
}

void UWorldForgeEmbarkLoadoutWidget::HandleCopperIngotsPlusClicked()
{
	AddItem(ItemCopperIngots);
}

void UWorldForgeEmbarkLoadoutWidget::PopulateSeedTypeDropdown()
{
	if (!SeedTypeDropdown)
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] SeedTypeDropdown not bound; skipping seed population."));
		return;
	}

	SeedTypeDropdown->ClearOptions();
	SeedDisplayNameToItemId.Reset();

	UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, DT_LowPolyPath));
	if (!DataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Failed to load DT_LowPoly from %s"), DT_LowPolyPath);
		SeedTypeDropdown->AddOption(TEXT("Seeds (generic)"));
		SeedTypeDropdown->SetSelectedOption(TEXT("Seeds (generic)"));
		SeedDisplayNameToItemId.Add(TEXT("Seeds (generic)"), ItemSeeds);
		ActiveSeedDisplayName = TEXT("Seeds (generic)");
		ActiveSeedItemId = ItemSeeds;
		return;
	}

	TArray<FName> RowNames = DataTable->GetRowNames();
	TArray<FString> DisplayNames;
	DisplayNames.Reserve(RowNames.Num());
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	const TArray<FString> DisplayNameColumns =
	{
		FString(TEXT("DisplayName")),
		FString(TEXT("Display Name"))
	};
	const TArray<FString> CropTypeColumns =
	{
		FString(TEXT("CropType")),
		FString(TEXT("Crop Type"))
	};

	for (const FName& RowName : RowNames)
	{
		const FString RowNameStr = RowName.ToString();
		const uint8* RowData = DataTable->FindRowUnchecked(RowName);
		const UScriptStruct* RowStruct = DataTable->GetRowStruct();
		FString DisplayName = ReadRowStringProperty(RowStruct, RowData, DisplayNameColumns);
		const FString CropType = ReadRowStringProperty(RowStruct, RowData, CropTypeColumns);

		// Final fallback: derive from row name
		if (DisplayName.IsEmpty())
		{
			DisplayName = CropType.IsEmpty() ? MakeDisplayNameFromRowName(RowNameStr) : CropType;
		}
		DisplayName = NormalizeSeedTypeDisplayName(DisplayName);
		if (DisplayName.IsEmpty())
		{
			continue;
		}

		// Build item ID: Seed.<CropTypeOrSanitizedDisplayName>
		const FString ItemIdSource = CropType.IsEmpty() ? DisplayName : CropType;
		const FString SanitizedName = SanitizeSeedName(ItemIdSource.IsEmpty() ? RowNameStr : ItemIdSource);
		if (SanitizedName.IsEmpty())
		{
			continue;
		}

		const FName SeedItemId = FName(*FString::Printf(TEXT("Seed.%s"), *SanitizedName));

		if (!SeedDisplayNameToItemId.Contains(DisplayName))
		{
			DisplayNames.Add(DisplayName);
			SeedDisplayNameToItemId.Add(DisplayName, SeedItemId);
			if (Manager)
			{
				Manager->RegisterDynamicSeedLoadoutOption(SeedItemId, MakeSeedItemDisplayName(DisplayName), 2);
			}
		}
	}

	// Sort alphabetically
	DisplayNames.Sort();

	for (const FString& Name : DisplayNames)
	{
		SeedTypeDropdown->AddOption(Name);
	}

	// Select first option by default
	if (DisplayNames.Num() > 0)
	{
		SeedTypeDropdown->SetSelectedOption(DisplayNames[0]);
		ActiveSeedDisplayName = DisplayNames[0];
		ActiveSeedItemId = SeedDisplayNameToItemId[DisplayNames[0]];
		UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Selected seed type: %s"), *ActiveSeedDisplayName);
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Loaded seed options from DT_LowPoly: %d"), DisplayNames.Num());
}

FName UWorldForgeEmbarkLoadoutWidget::GetActiveSeedItemId() const
{
	return ActiveSeedItemId.IsNone() ? ItemSeeds : ActiveSeedItemId;
}

void UWorldForgeEmbarkLoadoutWidget::HandleSeedTypeSelectionChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	const FName* FoundItemId = SeedDisplayNameToItemId.Find(SelectedItem);
	if (!FoundItemId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] Unknown seed type selected: %s"), *SelectedItem);
		return;
	}

	ActiveSeedDisplayName = SelectedItem;
	ActiveSeedItemId = *FoundItemId;

	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (Manager)
	{
		Manager->RegisterDynamicSeedLoadoutOption(ActiveSeedItemId, MakeSeedItemDisplayName(ActiveSeedDisplayName), 2);
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Selected seed type: %s"), *SelectedItem);
	UpdateLoadoutDisplay();
}

void UWorldForgeEmbarkLoadoutWidget::RestoreSetupWidget()
{
	UUserWidget* SetupWidget = SetupWidgetToRestore.Get();
	if (!SetupWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkLoadout] No Embark Setup widget was provided to restore."));
		return;
	}

	SetupWidget->SetVisibility(ESlateVisibility::Visible);
	SetupWidget->SetIsEnabled(true);
	if (UCommonActivatableWidget* CommonSetupWidget = Cast<UCommonActivatableWidget>(SetupWidget))
	{
		CommonSetupWidget->ActivateWidget();
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (PlayerController)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(SetupWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
		PlayerController->bShowMouseCursor = true;
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Restored Embark Setup widget."));
}

void UWorldForgeEmbarkLoadoutWidget::HandleBackClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Back clicked. Returning to Embark Setup."));
	RestoreSetupWidget();
	DeactivateWidget();
	RemoveFromParent();
}

void UWorldForgeEmbarkLoadoutWidget::HandleContinueClicked()
{
	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UpdateLoadoutDisplay(FText::FromString(TEXT("Embark loadout manager unavailable.")));
		return;
	}

	FString ValidationReason;
	if (!Manager->IsEmbarkLoadoutValid(ValidationReason))
	{
		Manager->LogEmbarkLoadoutContinue();
		UpdateLoadoutDisplay(FText::FromString(ValidationReason));
		return;
	}

	Manager->LogEmbarkLoadoutContinue();
	UpdateLoadoutDisplay(FText::GetEmpty());
	UE_LOG(LogTemp, Log, TEXT("[EmbarkLoadout] Continue accepted. Opening WorldGenTest."));
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/WorldForge/Levels/WorldGenTest")));
}

#include "BuildModeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Colony/SettlementManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Player/BuildModeManager.h"

namespace
{
	static const FVector2D WF_BuildModeToolbarButtonSize(86.0f, 38.0f);
	static const FVector2D WF_BuildModePopupButtonSize(150.0f, 34.0f);

	// Returns true if a prefab row should be hidden from the player-facing list
	// because it comes from a demo, plugin, or engine example package.
	static bool WF_IsDemoOrPluginPrefabID(const FString& PrefabID, const FString& Summary)
	{
		static const TArray<FString> BlockedKeywords = {
			TEXT("DemoPlatform"),
			TEXT("NiagaraExamples"),
			TEXT("Example"),
			TEXT("Debug"),
			TEXT("Test"),
			TEXT("Visualization"),
			TEXT("Viz"),
			TEXT("Tile_InnerCurve"),
			TEXT("Tile_OuterCurve"),
			TEXT("Tile_Corner"),
			TEXT("Button"),
			TEXT("Display"),
		};

		const FString Combined = PrefabID + TEXT(" ") + Summary;
		for (const FString& Keyword : BlockedKeywords)
		{
			if (Combined.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static FString WF_TrimmedLeftToken(const FString& Summary)
	{
		FString Left;
		FString Right;
		if (Summary.Split(TEXT("|"), &Left, &Right))
		{
			Left.TrimStartAndEndInline();
			return Left;
		}

		FString Result = Summary;
		Result.TrimStartAndEndInline();
		return Result;
	}

	static FString WF_HumanizeSeedName(FName SeedID)
	{
		FString Name = SeedID.IsNone() ? FString(TEXT("Unknown")) : SeedID.ToString();
		if (Name.StartsWith(TEXT("LP_")))
		{
			Name.RightChopInline(3);
		}

		Name.ReplaceInline(TEXT("_"), TEXT(" "));

		for (int32 Index = Name.Len() - 1; Index > 0; --Index)
		{
			const TCHAR Current = Name[Index];
			const TCHAR Previous = Name[Index - 1];
			if (FChar::IsUpper(Current) && FChar::IsLower(Previous))
			{
				Name.InsertAt(Index, TEXT(" "));
			}
		}

		Name.TrimStartAndEndInline();
		if (!Name.EndsWith(TEXT(" Seeds"), ESearchCase::IgnoreCase) &&
			!Name.EndsWith(TEXT(" Seed"), ESearchCase::IgnoreCase))
		{
			Name += TEXT(" Seeds");
		}

		return Name;
	}
}

void UBuildModeOptionButton::InitializeOption(
	UBuildModeWidget* InOwner,
	EBuildModeOptionButtonKind InKind,
	FName InOptionID)
{
	OwnerWidget = InOwner;
	OptionKind = InKind;
	OptionID = InOptionID;

	OnClicked.RemoveDynamic(this, &UBuildModeOptionButton::HandleClicked);
	OnClicked.AddDynamic(this, &UBuildModeOptionButton::HandleClicked);
}

void UBuildModeOptionButton::HandleClicked()
{
	if (UBuildModeWidget* Owner = OwnerWidget.Get())
	{
		Owner->HandleRuntimeOptionClicked(OptionKind, OptionID);
	}
}

void UBuildModeWidget::BuildCompactRuntimeLayout()
{
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = CanvasPanel_0;
	if (!RootCanvas)
	{
		RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	}

	if (!RootCanvas)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] Compact UI skipped: root widget is not a CanvasPanel."));
		return;
	}

	if (bRuntimeBuildModeLayoutCreated)
	{
		HideLegacyPlayerFacingWidgets();
		CollapseTransientPanels();
		return;
	}

	bRuntimeBuildModeLayoutCreated = true;

	BottomToolbar = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(),
		TEXT("BottomToolbar"));

	if (UCanvasPanelSlot* ToolbarSlot = RootCanvas->AddChildToCanvas(BottomToolbar))
	{
		ToolbarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		ToolbarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		ToolbarSlot->SetAutoSize(true);
		ToolbarSlot->SetPosition(FVector2D(0.0f, -24.0f));
	}

	BTN_Build = CreateRuntimeTextButton(TEXT("BTN_Build"), FText::FromString(TEXT("Build")), 14);
	BTN_Build->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleBuildClicked);
	AddToolbarEntry(BTN_Build, WF_BuildModeToolbarButtonSize);

	{
		UButton* DigButton = CreateRuntimeTextButton(TEXT("BTN_DigRuntime"), FText::FromString(TEXT("Dig")), 14);
		DigButton->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleDigChromeClicked);
		AddToolbarEntry(DigButton, WF_BuildModeToolbarButtonSize);
	}

	BTN_Mine = CreateRuntimeTextButton(TEXT("BTN_Mine"), FText::FromString(TEXT("Mine")), 14);
	BTN_Mine->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleMineChromeClicked);
	AddToolbarEntry(BTN_Mine, WF_BuildModeToolbarButtonSize);

	BTN_Farm = CreateRuntimeTextButton(TEXT("BTN_Farm"), FText::FromString(TEXT("Farm")), 14);
	BTN_Farm->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleFarmClicked);
	AddToolbarEntry(BTN_Farm, WF_BuildModeToolbarButtonSize);

	UButton* ConfirmButton = CreateRuntimeTextButton(TEXT("BTN_ConfirmCompact"), FText::FromString(TEXT("Confirm")), 14);
	ConfirmButton->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleConfirmChromeClicked);
	AddToolbarEntry(ConfirmButton, WF_BuildModeToolbarButtonSize);

	{
		UButton* CancelButton = CreateRuntimeTextButton(TEXT("BTN_CancelRuntime"), FText::FromString(TEXT("Cancel")), 14);
		CancelButton->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleCancelChromeClicked);
		AddToolbarEntry(CancelButton, WF_BuildModeToolbarButtonSize);
	}

	CreateBuildPopup(RootCanvas);
	CreateBuildPiecePanel(RootCanvas);
	CreatePrefabPanel(RootCanvas);
	CreateFarmSeedsPanel(RootCanvas);
	CreateStatusToast(RootCanvas);

	HideLegacyPlayerFacingWidgets();
	CollapseTransientPanels();
}

void UBuildModeWidget::HideLegacyPlayerFacingWidgets()
{
	HideRuntimeWidget(HouseButton);
	HideRuntimeWidget(FreeBuildButton);
	HideRuntimeWidget(BTN_Dig);
	HideRuntimeWidget(BTN_Confirm);
	HideRuntimeWidget(BTN_Cancel);
	HideRuntimeWidget(BTN_ToggleGrid);
	HideRuntimeWidget(CB_ModeSelect);
	HideRuntimeWidget(TXT_Dig);
	HideRuntimeWidget(TXT_Cancel);
	HideRuntimeWidget(TXT_Mode);
	HideRuntimeWidget(TXT_Building);
	HideRuntimeWidget(TXT_Material);
	HideRuntimeWidget(TXT_HoveredCell);
	HideRuntimeWidget(TXT_CellCount);
	HideRuntimeWidget(TXT_Status);
	HideRuntimeWidget(SizeBox_0);

	if (WidgetTree)
	{
		HideRuntimeWidget(WidgetTree->FindWidget(FName(TEXT("TextBlock_59"))));
		HideRuntimeWidget(WidgetTree->FindWidget(FName(TEXT("TextBlock"))));
		HideRuntimeWidget(WidgetTree->FindWidget(FName(TEXT("TXT_ToggleGrid"))));
	}
}

void UBuildModeWidget::HideRuntimeWidget(UWidget* Widget) const
{
	if (Widget)
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

UTextBlock* UBuildModeWidget::CreateRuntimeTextBlock(FName Name, const FText& Text, int32 FontSize)
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	TextBlock->SetText(Text);
	TextBlock->SetJustification(ETextJustify::Center);
	TextBlock->SetAutoWrapText(true);
	TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));

	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	TextBlock->SetFont(Font);

	return TextBlock;
}

UButton* UBuildModeWidget::CreateRuntimeTextButton(FName Name, const FText& Text, int32 FontSize)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetContent(CreateRuntimeTextBlock(
		MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), *FString::Printf(TEXT("TXT_%s"), *Name.ToString())),
		Text,
		FontSize));
	return Button;
}

void UBuildModeWidget::AddToolbarEntry(UWidget* Widget, const FVector2D& Size)
{
	if (!BottomToolbar || !Widget)
	{
		return;
	}

	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		MakeUniqueObjectName(WidgetTree, USizeBox::StaticClass(), TEXT("ToolbarButtonBox")));
	SizeBox->SetWidthOverride(Size.X);
	SizeBox->SetHeightOverride(Size.Y);
	SizeBox->SetContent(Widget);

	if (UHorizontalBoxSlot* ToolbarEntrySlot = BottomToolbar->AddChildToHorizontalBox(SizeBox))
	{
		ToolbarEntrySlot->SetPadding(FMargin(4.0f, 0.0f));
		ToolbarEntrySlot->SetHorizontalAlignment(HAlign_Center);
		ToolbarEntrySlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UBuildModeWidget::AddSizedVerticalEntry(UVerticalBox* Parent, UWidget* Widget, const FVector2D& Size, const FMargin& EntryPadding)
{
	if (!Parent || !Widget)
	{
		return;
	}

	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		MakeUniqueObjectName(WidgetTree, USizeBox::StaticClass(), TEXT("PanelEntryBox")));
	SizeBox->SetWidthOverride(Size.X);
	SizeBox->SetHeightOverride(Size.Y);
	SizeBox->SetContent(Widget);

	if (UVerticalBoxSlot* VerticalEntrySlot = Parent->AddChildToVerticalBox(SizeBox))
	{
		VerticalEntrySlot->SetPadding(EntryPadding);
	}
}

void UBuildModeWidget::CreateBuildPopup(UCanvasPanel* RootCanvas)
{
	Panel_BuildPopup = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Panel_BuildPopup"));

	BTN_OpenBuildPieces = CreateRuntimeTextButton(TEXT("BTN_OpenBuildPieces"), FText::FromString(TEXT("BuildPieces")), 13);
	BTN_OpenBuildPieces->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleOpenBuildPiecesClicked);
	AddSizedVerticalEntry(Panel_BuildPopup, BTN_OpenBuildPieces, WF_BuildModePopupButtonSize, FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	BTN_OpenPrefabs = CreateRuntimeTextButton(TEXT("BTN_OpenPrefabs"), FText::FromString(TEXT("Prefabs")), 13);
	BTN_OpenPrefabs->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleOpenPrefabsClicked);
	AddSizedVerticalEntry(Panel_BuildPopup, BTN_OpenPrefabs, WF_BuildModePopupButtonSize, FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	BTN_OpenFreeBuild = CreateRuntimeTextButton(TEXT("BTN_OpenFreeBuild"), FText::FromString(TEXT("Free Build")), 13);
	BTN_OpenFreeBuild->OnClicked.AddDynamic(this, &UBuildModeWidget::HandleOpenFreeBuildClicked);
	AddSizedVerticalEntry(Panel_BuildPopup, BTN_OpenFreeBuild, WF_BuildModePopupButtonSize, FMargin(0.0f));

	if (UCanvasPanelSlot* BuildPopupSlot = RootCanvas->AddChildToCanvas(Panel_BuildPopup))
	{
		BuildPopupSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		BuildPopupSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BuildPopupSlot->SetAutoSize(true);
		BuildPopupSlot->SetPosition(FVector2D(-144.0f, -76.0f));
	}
}

void UBuildModeWidget::CreateBuildPiecePanel(UCanvasPanel* RootCanvas)
{
	Panel_BuildPieceList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Panel_BuildPieceList"));

	UTextBlock* Header = CreateRuntimeTextBlock(TEXT("Text_BuildPieceListHeader"), FText::FromString(TEXT("BuildPieces")), 14);
	if (UVerticalBoxSlot* HeaderSlot = Panel_BuildPieceList->AddChildToVerticalBox(Header))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	ScrollBox_BuildPieceOptions = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ScrollBox_BuildPieceOptions"));
	if (UVerticalBoxSlot* ScrollSlot = Panel_BuildPieceList->AddChildToVerticalBox(ScrollBox_BuildPieceOptions))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (UCanvasPanelSlot* BuildPieceListSlot = RootCanvas->AddChildToCanvas(Panel_BuildPieceList))
	{
		BuildPieceListSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		BuildPieceListSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		BuildPieceListSlot->SetPosition(FVector2D(24.0f, -86.0f));
		BuildPieceListSlot->SetSize(FVector2D(380.0f, 320.0f));
	}
}

void UBuildModeWidget::CreatePrefabPanel(UCanvasPanel* RootCanvas)
{
	Panel_PrefabList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Panel_PrefabList"));

	UTextBlock* Header = CreateRuntimeTextBlock(TEXT("Text_PrefabListHeader"), FText::FromString(TEXT("Prefabs")), 14);
	if (UVerticalBoxSlot* HeaderSlot = Panel_PrefabList->AddChildToVerticalBox(Header))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	ScrollBox_PrefabOptions = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ScrollBox_PrefabOptions"));
	if (UVerticalBoxSlot* ScrollSlot = Panel_PrefabList->AddChildToVerticalBox(ScrollBox_PrefabOptions))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (UCanvasPanelSlot* PrefabListSlot = RootCanvas->AddChildToCanvas(Panel_PrefabList))
	{
		PrefabListSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		PrefabListSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		PrefabListSlot->SetPosition(FVector2D(24.0f, -86.0f));
		PrefabListSlot->SetSize(FVector2D(380.0f, 320.0f));
	}
}

void UBuildModeWidget::CreateFarmSeedsPanel(UCanvasPanel* RootCanvas)
{
	Panel_FarmSeeds = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Panel_FarmSeeds"));

	Text_FarmSeedsHeader = CreateRuntimeTextBlock(TEXT("Text_FarmSeedsHeader"), FText::FromString(TEXT("Farm Seeds")), 14);
	if (UVerticalBoxSlot* HeaderSlot = Panel_FarmSeeds->AddChildToVerticalBox(Text_FarmSeedsHeader))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	Text_NoSeedsAvailable = CreateRuntimeTextBlock(TEXT("Text_NoSeedsAvailable"), FText::FromString(TEXT("No seeds available")), 13);
	if (UVerticalBoxSlot* EmptySlot = Panel_FarmSeeds->AddChildToVerticalBox(Text_NoSeedsAvailable))
	{
		EmptySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	ScrollBox_SeedOptions = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ScrollBox_SeedOptions"));
	if (UVerticalBoxSlot* ScrollSlot = Panel_FarmSeeds->AddChildToVerticalBox(ScrollBox_SeedOptions))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (UCanvasPanelSlot* FarmSeedsSlot = RootCanvas->AddChildToCanvas(Panel_FarmSeeds))
	{
		FarmSeedsSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		FarmSeedsSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		FarmSeedsSlot->SetPosition(FVector2D(-24.0f, -86.0f));
		FarmSeedsSlot->SetSize(FVector2D(300.0f, 280.0f));
	}
}

void UBuildModeWidget::CreateStatusToast(UCanvasPanel* RootCanvas)
{
	Text_StatusToast = CreateRuntimeTextBlock(TEXT("Text_StatusToast"), FText::GetEmpty(), 13);
	Text_StatusToast->SetVisibility(ESlateVisibility::Collapsed);

	if (UCanvasPanelSlot* StatusToastSlot = RootCanvas->AddChildToCanvas(Text_StatusToast))
	{
		StatusToastSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		StatusToastSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		StatusToastSlot->SetAutoSize(true);
		StatusToastSlot->SetPosition(FVector2D(0.0f, -132.0f));
	}
}

void UBuildModeWidget::CollapseTransientPanels()
{
	if (Panel_BuildPopup)
	{
		Panel_BuildPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_PrefabList)
	{
		Panel_PrefabList->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_BuildPieceList)
	{
		Panel_BuildPieceList->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_FarmSeeds)
	{
		Panel_FarmSeeds->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UBuildModeWidget::ShowStatusToast(const FText& Message)
{
	if (!Text_StatusToast)
	{
		return;
	}

	Text_StatusToast->SetText(Message);
	Text_StatusToast->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UBuildModeWidget::HandleBuildClicked()
{
	if (!Panel_BuildPopup)
	{
		return;
	}

	const bool bShouldOpen = Panel_BuildPopup->GetVisibility() != ESlateVisibility::Visible;
	CollapseTransientPanels();
	Panel_BuildPopup->SetVisibility(bShouldOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UBuildModeWidget::HandleOpenBuildPiecesClicked()
{
	if (Panel_BuildPopup)
	{
		Panel_BuildPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_PrefabList)
	{
		Panel_PrefabList->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_FarmSeeds)
	{
		Panel_FarmSeeds->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_BuildPieceList)
	{
		Panel_BuildPieceList->SetVisibility(ESlateVisibility::Visible);
	}

	RefreshBuildPieceList();
}

void UBuildModeWidget::HandleOpenPrefabsClicked()
{
	if (Panel_BuildPopup)
	{
		Panel_BuildPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_BuildPieceList)
	{
		Panel_BuildPieceList->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_FarmSeeds)
	{
		Panel_FarmSeeds->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_PrefabList)
	{
		Panel_PrefabList->SetVisibility(ESlateVisibility::Visible);
	}

	RefreshPrefabList();
}

void UBuildModeWidget::HandleOpenFreeBuildClicked()
{
	CollapseTransientPanels();

	const FGameplayTag DefaultMaterial = FGameplayTag::RequestGameplayTag(
		FName(TEXT("WorldForge.Voxel.Material.Stone")));
	SelectMaterial(DefaultMaterial);
	ShowStatusToast(FText::FromString(TEXT("Free build selected")));
}

void UBuildModeWidget::HandleFarmClicked()
{
	if (Panel_BuildPopup)
	{
		Panel_BuildPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_BuildPieceList)
	{
		Panel_BuildPieceList->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Panel_PrefabList)
	{
		Panel_PrefabList->SetVisibility(ESlateVisibility::Collapsed);
	}

	SwitchToFarmMode();

	if (Panel_FarmSeeds)
	{
		Panel_FarmSeeds->SetVisibility(ESlateVisibility::Visible);
	}
	RefreshFarmSeedPanel();
}

void UBuildModeWidget::HandleDigChromeClicked()
{
	CollapseTransientPanels();
	SwitchToDigMode();
	ShowStatusToast(FText::FromString(TEXT("Dig mode selected")));
}

void UBuildModeWidget::HandleMineChromeClicked()
{
	CollapseTransientPanels();
	SwitchToMineMode();
}

void UBuildModeWidget::HandleConfirmChromeClicked()
{
	CollapseTransientPanels();
	ConfirmBuild();
}

void UBuildModeWidget::HandleCancelChromeClicked()
{
	CollapseTransientPanels();
	CancelBuild();
	ShowStatusToast(FText::FromString(TEXT("Cancelled")));
}

void UBuildModeWidget::HandleNativeBuildFeedbackMessage(const FString& Message)
{
	if (!Message.IsEmpty())
	{
		ShowStatusToast(FText::FromString(Message));
	}
}

void UBuildModeWidget::RefreshBuildPieceList()
{
	if (!ScrollBox_BuildPieceOptions)
	{
		return;
	}

	ScrollBox_BuildPieceOptions->ClearChildren();

	const TArray<FString> Summaries = GetBuildDataBuildPieceSummaries(NAME_None);

	constexpr int32 WF_MaxBuildPieceListCount = 100;
	int32 AddedCount = 0;
	bool bAnyAdded = false;

	for (const FString& Summary : Summaries)
	{
		if (AddedCount >= WF_MaxBuildPieceListCount)
		{
			break;
		}

		const FName BuildID = ParseBuildPieceIDFromSummary(Summary);
		if (BuildID.IsNone())
		{
			continue;
		}

		bAnyAdded = true;
		++AddedCount;

		UBuildModeOptionButton* OptionButton = WidgetTree->ConstructWidget<UBuildModeOptionButton>(
			UBuildModeOptionButton::StaticClass(),
			MakeUniqueObjectName(WidgetTree, UBuildModeOptionButton::StaticClass(), TEXT("BTN_BuildPieceOption")));
		OptionButton->InitializeOption(this, EBuildModeOptionButtonKind::BuildPiece, BuildID);
		OptionButton->SetContent(CreateRuntimeTextBlock(
			MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("TXT_BuildPieceOption")),
			FText::FromString(Summary),
			12));

		if (UScrollBoxSlot* BuildPieceOptionSlot = Cast<UScrollBoxSlot>(ScrollBox_BuildPieceOptions->AddChild(OptionButton)))
		{
			BuildPieceOptionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}
	}

	if (!bAnyAdded)
	{
		ScrollBox_BuildPieceOptions->AddChild(CreateRuntimeTextBlock(
			MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("Text_NoBuildPiecesAvailable")),
			FText::FromString(TEXT("No build pieces available")),
			13));
	}
}

void UBuildModeWidget::RefreshPrefabList()
{
	if (!ScrollBox_PrefabOptions)
	{
		return;
	}

	ScrollBox_PrefabOptions->ClearChildren();

	const TArray<FString> Summaries = GetBuildDataPrefabSummaries(NAME_None);

	constexpr int32 WF_MaxPrefabListCount = 50;
	int32 AddedCount = 0;
	bool bAnyAdded = false;

	for (const FString& Summary : Summaries)
	{
		if (AddedCount >= WF_MaxPrefabListCount)
		{
			break;
		}

		const FName PrefabID = ParsePrefabIDFromSummary(Summary);
		if (PrefabID.IsNone())
		{
			continue;
		}

		if (WF_IsDemoOrPluginPrefabID(PrefabID.ToString(), Summary))
		{
			UE_LOG(LogTemp, Verbose, TEXT("[BuildModeWidget] Prefab filtered (demo/plugin): %s"), *Summary);
			continue;
		}

		bAnyAdded = true;
		++AddedCount;

		UBuildModeOptionButton* OptionButton = WidgetTree->ConstructWidget<UBuildModeOptionButton>(
			UBuildModeOptionButton::StaticClass(),
			MakeUniqueObjectName(WidgetTree, UBuildModeOptionButton::StaticClass(), TEXT("BTN_PrefabOption")));
		OptionButton->InitializeOption(this, EBuildModeOptionButtonKind::Prefab, PrefabID);
		OptionButton->SetContent(CreateRuntimeTextBlock(
			MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("TXT_PrefabOption")),
			FText::FromString(Summary),
			12));

		if (UScrollBoxSlot* PrefabOptionSlot = Cast<UScrollBoxSlot>(ScrollBox_PrefabOptions->AddChild(OptionButton)))
		{
			PrefabOptionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}
	}

	if (!bAnyAdded)
	{
		ScrollBox_PrefabOptions->AddChild(CreateRuntimeTextBlock(
			MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("Text_NoPrefabsAvailable")),
			FText::FromString(TEXT("No prefabs available")),
			13));
	}
}

void UBuildModeWidget::RefreshFarmSeedPanel()
{
	if (!ScrollBox_SeedOptions || !Text_NoSeedsAvailable)
	{
		return;
	}

	ScrollBox_SeedOptions->ClearChildren();

	const TArray<FBuildModeFarmSeedSummary> Seeds = GetAvailableFarmSeedSummaries();
	const bool bHasSeeds = Seeds.Num() > 0;
	Text_NoSeedsAvailable->SetVisibility(bHasSeeds ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	if (!bHasSeeds)
	{
		ShowStatusToast(FText::FromString(TEXT("No seeds available")));
		return;
	}

	for (const FBuildModeFarmSeedSummary& Seed : Seeds)
	{
		const FText Label = FText::Format(
			NSLOCTEXT("WorldForge", "BuildModeSeedOption", "{0} x{1}"),
			Seed.DisplayName,
			FText::AsNumber(Seed.Quantity));

		UBuildModeOptionButton* OptionButton = WidgetTree->ConstructWidget<UBuildModeOptionButton>(
			UBuildModeOptionButton::StaticClass(),
			MakeUniqueObjectName(WidgetTree, UBuildModeOptionButton::StaticClass(), TEXT("BTN_SeedOption")));
		OptionButton->InitializeOption(this, EBuildModeOptionButtonKind::FarmSeed, Seed.SeedID);
		OptionButton->SetContent(CreateRuntimeTextBlock(
			MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("TXT_SeedOption")),
			Label,
			13));

		if (UScrollBoxSlot* SeedOptionSlot = Cast<UScrollBoxSlot>(ScrollBox_SeedOptions->AddChild(OptionButton)))
		{
			SeedOptionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}
	}
}

FName UBuildModeWidget::ParseBuildPieceIDFromSummary(const FString& Summary) const
{
	const FString Token = WF_TrimmedLeftToken(Summary);
	return Token.IsEmpty() ? NAME_None : FName(*Token);
}

FName UBuildModeWidget::ParsePrefabIDFromSummary(const FString& Summary) const
{
	const FString Token = WF_TrimmedLeftToken(Summary);
	return Token.IsEmpty() ? NAME_None : FName(*Token);
}

TArray<FBuildModeFarmSeedSummary> UBuildModeWidget::GetAvailableFarmSeedSummaries() const
{
	TArray<FBuildModeFarmSeedSummary> Result;

	const UWorld* World = GetWorld();
	const USettlementManager* SettlementManager = World ? World->GetSubsystem<USettlementManager>() : nullptr;
	if (!SettlementManager || !SettlementManager->HasPlayerSettlement())
	{
		return Result;
	}

	const int32 SettlementID = SettlementManager->GetPlayerSettlementID();
	const TMap<FName, int32> SeedInventory = SettlementManager->GetStartingSeedInventory(SettlementID);
	const TArray<FName> SeedOrder = SettlementManager->GetStartingSeedInventoryOrder(SettlementID);

	TSet<FName> AddedSeeds;
	auto AddSeedSummary = [this, &Result, &SeedInventory, &AddedSeeds](FName SeedID)
		{
			if (SeedID.IsNone() || AddedSeeds.Contains(SeedID))
			{
				return;
			}

			const int32* QuantityPtr = SeedInventory.Find(SeedID);
			const int32 Quantity = QuantityPtr ? FMath::Max(0, *QuantityPtr) : 0;
			if (Quantity <= 0)
			{
				return;
			}

			FBuildModeFarmSeedSummary Summary;
			Summary.SeedID = SeedID;
			Summary.DisplayName = MakeFarmSeedDisplayName(SeedID);
			Summary.Quantity = Quantity;
			Result.Add(Summary);
			AddedSeeds.Add(SeedID);
		};

	for (const FName& SeedID : SeedOrder)
	{
		AddSeedSummary(SeedID);
	}

	TArray<FName> RemainingSeedIDs;
	SeedInventory.GenerateKeyArray(RemainingSeedIDs);
	RemainingSeedIDs.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});
	for (const FName& SeedID : RemainingSeedIDs)
	{
		AddSeedSummary(SeedID);
	}

	return Result;
}

FText UBuildModeWidget::MakeFarmSeedDisplayName(FName SeedID) const
{
	return FText::FromString(WF_HumanizeSeedName(SeedID));
}

void UBuildModeWidget::SelectFarmSeedFromInventory(FName SeedID)
{
	if (SeedID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] Farm seed selection ignored: SeedID is None."));
		ShowStatusToast(FText::FromString(TEXT("No seeds available")));
		return;
	}

	const TArray<FBuildModeFarmSeedSummary> Seeds = GetAvailableFarmSeedSummaries();
	const FBuildModeFarmSeedSummary* SelectedSeed = Seeds.FindByPredicate([SeedID](const FBuildModeFarmSeedSummary& Candidate)
		{
			return Candidate.SeedID == SeedID;
		});

	if (!SelectedSeed)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] Farm seed selection ignored: %s is not available."), *SeedID.ToString());
		ShowStatusToast(FText::FromString(TEXT("No seeds available")));
		RefreshFarmSeedPanel();
		return;
	}

	if (UBuildModeManager* Mgr = GetManager())
	{
		Mgr->SetSelectedFarmCropSeed(SeedID);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BuildModeWidget] Farm seed selected: SeedID=%s DisplayName=%s Quantity=%d"),
		*SeedID.ToString(),
		*SelectedSeed->DisplayName.ToString(),
		SelectedSeed->Quantity);

	ShowStatusToast(FText::Format(
		NSLOCTEXT("WorldForge", "BuildModeFarmSeedSelected", "Farm seed selected: {0} x{1}"),
		SelectedSeed->DisplayName,
		FText::AsNumber(SelectedSeed->Quantity)));

	RefreshFarmSeedPanel();
}

void UBuildModeWidget::HandleRuntimeOptionClicked(EBuildModeOptionButtonKind Kind, FName OptionID)
{
	switch (Kind)
	{
	case EBuildModeOptionButtonKind::BuildPiece:
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] BuildPiece button clicked: %s"), *OptionID.ToString());
		SelectBuildPieceFromDataTable(OptionID);
		ShowStatusToast(FText::FromString(TEXT("BuildPiece selected")));
		break;

	case EBuildModeOptionButtonKind::Prefab:
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Prefab button clicked: %s"), *OptionID.ToString());
		SelectBuildingPrefabFromDataTable(OptionID);
		ShowStatusToast(FText::FromString(TEXT("Prefab selected")));
		break;

	case EBuildModeOptionButtonKind::FarmSeed:
		SelectFarmSeedFromInventory(OptionID);
		break;

	default:
		break;
	}
}

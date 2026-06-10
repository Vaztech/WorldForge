#include "UI/WorldForgeSaveSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
constexpr int32 SaveSelectFixedSlotCount = 8;

const TCHAR* BindingStatus(const UObject* Widget)
{
    return Widget ? TEXT("OK") : TEXT("NULL");
}

FString MakeFixedSaveSlotName(const int32 SlotIndex)
{
    return FString::Printf(TEXT("WorldForge_Slot%d"), SlotIndex);
}

FWorldForgeSaveSlotSummary MakeEmptySlotSummary(const int32 SlotIndex)
{
    FWorldForgeSaveSlotSummary Summary;
    Summary.SlotIndex = SlotIndex;
    Summary.SaveSlotName = MakeFixedSaveSlotName(SlotIndex);
    Summary.WorldName = TEXT("Empty Slot");
    Summary.bHasSave = false;
    return Summary;
}

FString MakeDetailLine(const TCHAR* Label, const FString& Value)
{
    return FString::Printf(TEXT("%s: %s"), Label, *Value);
}
}

void UWorldForgeSaveSlotButton::InitializeSlot(const FWorldForgeSaveSlotSummary& InSummary)
{
    Summary = InSummary;
    SlotIndex = InSummary.SlotIndex;
    SaveSlotName = InSummary.SaveSlotName;
}

void UWorldForgeSaveSlotButton::HandleClicked()
{
    OnSaveSlotClicked.Broadcast(this, SlotIndex);
}

void UWorldForgeSaveSelectWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UE_LOG(LogTemp, Log,
        TEXT("[SaveSelect] Constructed. PNL_SaveSlotList=%s TXT_DetailWorldName=%s TXT_DetailLastPlayed=%s TXT_DetailColonyName=%s TXT_DetailDayYear=%s TXT_DetailSaveVersion=%s BTN_LoadSelected=%s BTN_DeleteSelected=%s BTN_Back=%s"),
        BindingStatus(PNL_SaveSlotList),
        BindingStatus(TXT_DetailWorldName),
        BindingStatus(TXT_DetailLastPlayed),
        BindingStatus(TXT_DetailColonyName),
        BindingStatus(TXT_DetailDayYear),
        BindingStatus(TXT_DetailSaveVersion),
        BindingStatus(BTN_LoadSelected),
        BindingStatus(BTN_DeleteSelected),
        BindingStatus(BTN_Back));

    BindControls();
    RefreshSaveSlots();
}

void UWorldForgeSaveSelectWidget::SetPreviousWidget(UUserWidget* InWidget)
{
    PreviousWidget = InWidget;
}

void UWorldForgeSaveSelectWidget::BindControls()
{
    if (BTN_LoadSelected)
    {
        BTN_LoadSelected->OnClicked.RemoveDynamic(this, &UWorldForgeSaveSelectWidget::HandleLoadSelectedClicked);
        BTN_LoadSelected->OnClicked.AddDynamic(this, &UWorldForgeSaveSelectWidget::HandleLoadSelectedClicked);
    }

    if (BTN_DeleteSelected)
    {
        BTN_DeleteSelected->OnClicked.RemoveDynamic(this, &UWorldForgeSaveSelectWidget::HandleDeleteSelectedClicked);
        BTN_DeleteSelected->OnClicked.AddDynamic(this, &UWorldForgeSaveSelectWidget::HandleDeleteSelectedClicked);
    }

    if (BTN_Back)
    {
        BTN_Back->OnClicked.RemoveDynamic(this, &UWorldForgeSaveSelectWidget::HandleBackClicked);
        BTN_Back->OnClicked.AddDynamic(this, &UWorldForgeSaveSelectWidget::HandleBackClicked);
    }
}

void UWorldForgeSaveSelectWidget::RefreshSaveSlots()
{
    SaveSlotSummaries.Reset();
    SelectedSlotIndex = INDEX_NONE;

    if (const UGameInstance* GameInstance = GetGameInstance())
    {
        if (const USaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<USaveSubsystem>())
        {
            SaveSlotSummaries = SaveSubsystem->GetFixedSaveSlotSummaries(SaveSelectFixedSlotCount);
        }
    }

    if (SaveSlotSummaries.Num() != SaveSelectFixedSlotCount)
    {
        TArray<FWorldForgeSaveSlotSummary> NormalizedSummaries;
        NormalizedSummaries.Reserve(SaveSelectFixedSlotCount);

        for (int32 SlotIndex = 1; SlotIndex <= SaveSelectFixedSlotCount; ++SlotIndex)
        {
            if (const FWorldForgeSaveSlotSummary* ExistingSummary = SaveSlotSummaries.FindByPredicate(
                [SlotIndex](const FWorldForgeSaveSlotSummary& Summary)
                {
                    return Summary.SlotIndex == SlotIndex;
                }))
            {
                NormalizedSummaries.Add(*ExistingSummary);
            }
            else
            {
                NormalizedSummaries.Add(MakeEmptySlotSummary(SlotIndex));
            }
        }

        SaveSlotSummaries = MoveTemp(NormalizedSummaries);
    }

    UE_LOG(LogTemp, Log, TEXT("[SaveSelect] Found %d save slots."), SaveSlotSummaries.Num());

    const bool bHasAnySave = SaveSlotSummaries.ContainsByPredicate(
        [](const FWorldForgeSaveSlotSummary& Summary)
        {
            return Summary.bHasSave;
        });

    if (!bHasAnySave)
    {
        UE_LOG(LogTemp, Log, TEXT("[SaveSelect] No saves found."));
    }

    SetStatusText(bHasAnySave ? TEXT("Select a save slot.") : TEXT("No saves found."));

    RebuildSlotList();

    if (SaveSlotSummaries.Num() > 0)
    {
        SelectSlotByIndex(SaveSlotSummaries[0].SlotIndex);
    }
    else
    {
        UpdateDetailsForSelectedSlot();
    }
}

void UWorldForgeSaveSelectWidget::RebuildSlotList()
{
    if (!PNL_SaveSlotList || !WidgetTree)
    {
        return;
    }

    PNL_SaveSlotList->ClearChildren();

    for (const FWorldForgeSaveSlotSummary& Summary : SaveSlotSummaries)
    {
        const FName ButtonName(*FString::Printf(TEXT("BTN_SaveSlot_%d"), Summary.SlotIndex));
        UWorldForgeSaveSlotButton* SlotButton = WidgetTree->ConstructWidget<UWorldForgeSaveSlotButton>(
            UWorldForgeSaveSlotButton::StaticClass(),
            ButtonName);
        if (!SlotButton)
        {
            continue;
        }

        SlotButton->InitializeSlot(Summary);
        SlotButton->SetIsEnabled(true);
        SlotButton->SetBackgroundColor(Summary.bHasSave
            ? FLinearColor(0.16f, 0.19f, 0.23f, 1.0f)
            : FLinearColor(0.10f, 0.115f, 0.135f, 1.0f));
        SlotButton->OnClicked.RemoveDynamic(SlotButton, &UWorldForgeSaveSlotButton::HandleClicked);
        SlotButton->OnClicked.AddDynamic(SlotButton, &UWorldForgeSaveSlotButton::HandleClicked);
        SlotButton->OnSaveSlotClicked.AddDynamic(this, &UWorldForgeSaveSelectWidget::HandleSlotButtonClicked);

        const FName TextName(*FString::Printf(TEXT("TXT_SaveSlot_%d"), Summary.SlotIndex));
        UTextBlock* SlotText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TextName);
        if (SlotText)
        {
            SlotText->SetText(FText::FromString(MakeSlotButtonLabel(Summary)));
            SlotText->SetAutoWrapText(true);
            SlotText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.98f, 1.0f, 1.0f)));
            SlotButton->SetContent(SlotText);
        }

        UPanelSlot* AddedSlot = PNL_SaveSlotList->AddChild(SlotButton);
        if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(AddedSlot))
        {
            VerticalSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
            VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
        }
    }
}

void UWorldForgeSaveSelectWidget::HandleSlotButtonClicked(UWorldForgeSaveSlotButton* /*Button*/, const int32 SlotIndex)
{
    SelectSlotByIndex(SlotIndex);
}

void UWorldForgeSaveSelectWidget::SelectSlotByIndex(const int32 SlotIndex)
{
    const FWorldForgeSaveSlotSummary* Summary = FindSummaryBySlotIndex(SlotIndex);
    if (!Summary)
    {
        return;
    }

    SelectedSlotIndex = SlotIndex;
    UE_LOG(LogTemp, Log, TEXT("[SaveSelect] Slot selected. Index=%d SaveSlotName='%s'"),
        Summary->SlotIndex,
        *Summary->SaveSlotName);

    SetStatusText(Summary->bHasSave ? TEXT("Save selected.") : TEXT("Empty slot selected."));

    UpdateDetailsForSelectedSlot();
}

void UWorldForgeSaveSelectWidget::UpdateDetailsForSelectedSlot()
{
    const FWorldForgeSaveSlotSummary* Summary = FindSummaryBySlotIndex(SelectedSlotIndex);
    if (!Summary || !Summary->bHasSave)
    {
        SetDetailText(TXT_DetailWorldName, MakeDetailLine(TEXT("World Name"), TEXT("Empty Slot")));
        SetDetailText(TXT_DetailLastPlayed, MakeDetailLine(TEXT("Last Played"), TEXT("")));
        SetDetailText(TXT_DetailColonyName, MakeDetailLine(TEXT("Colony Name"), TEXT("")));
        SetDetailText(TXT_DetailDayYear, MakeDetailLine(TEXT("Day / Year"), TEXT("")));
        SetDetailText(TXT_DetailSaveVersion, MakeDetailLine(TEXT("Save Version"), TEXT("")));
        return;
    }

    SetDetailText(TXT_DetailWorldName, MakeDetailLine(TEXT("World Name"), Summary->WorldName));
    SetDetailText(TXT_DetailLastPlayed, MakeDetailLine(TEXT("Last Played"), Summary->LastPlayedText));
    SetDetailText(TXT_DetailColonyName, MakeDetailLine(TEXT("Colony Name"), Summary->ColonyName));
    SetDetailText(TXT_DetailDayYear, MakeDetailLine(TEXT("Day / Year"), Summary->DayYearText));
    SetDetailText(TXT_DetailSaveVersion, MakeDetailLine(TEXT("Save Version"), Summary->SaveVersionText));
}

void UWorldForgeSaveSelectWidget::HandleLoadSelectedClicked()
{
    const FWorldForgeSaveSlotSummary* Summary = FindSummaryBySlotIndex(SelectedSlotIndex);
    const FString SaveSlotName = Summary ? Summary->SaveSlotName : FString();
    UE_LOG(LogTemp, Log, TEXT("[SaveSelect] Load selected placeholder. SaveSlotName='%s'"), *SaveSlotName);
}

void UWorldForgeSaveSelectWidget::HandleDeleteSelectedClicked()
{
    const FWorldForgeSaveSlotSummary* Summary = FindSummaryBySlotIndex(SelectedSlotIndex);
    if (!Summary)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSelect] No save selected."));
        SetStatusText(TEXT("No save selected."));
        return;
    }

    const int32 DeletedSlotIndex = Summary->SlotIndex;
    const FString SaveSlotName = Summary->SaveSlotName;

    UE_LOG(LogTemp, Log, TEXT("[SaveSelect] Delete selected requested. SaveSlotName='%s'"), *SaveSlotName);

    if (!Summary->bHasSave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSelect] Cannot delete empty slot."));
        SetStatusText(TEXT("Cannot delete empty slot."));
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    USaveSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<USaveSubsystem>() : nullptr;
    if (!SaveSubsystem)
    {
        const FString Reason = TEXT("Save subsystem unavailable.");
        UE_LOG(LogTemp, Error, TEXT("[SaveSelect] Delete failed. Reason='%s'"), *Reason);
        SetStatusText(Reason);
        return;
    }

    FString Reason;
    // TODO: Replace direct delete with confirmation dialog before release.
    if (!SaveSubsystem->DeleteSaveSlot(SaveSlotName, Reason))
    {
        if (Reason.IsEmpty())
        {
            Reason = TEXT("Delete failed.");
        }

        UE_LOG(LogTemp, Warning, TEXT("[SaveSelect] Delete failed. Reason='%s'"), *Reason);
        SetStatusText(Reason);
        return;
    }

    RefreshSaveSlots();
    SelectSlotByIndex(DeletedSlotIndex);
    SetStatusText(TEXT("Save deleted."));
}

void UWorldForgeSaveSelectWidget::HandleBackClicked()
{
    UE_LOG(LogTemp, Log, TEXT("[SaveSelect] Back clicked."));
    RestorePreviousWidget();
    DeactivateWidget();
    RemoveFromParent();
}

void UWorldForgeSaveSelectWidget::RestorePreviousWidget()
{
    UUserWidget* Widget = PreviousWidget.Get();
    if (!Widget)
    {
        return;
    }

    Widget->SetVisibility(ESlateVisibility::Visible);
    Widget->SetIsEnabled(true);

    if (UCommonActivatableWidget* CommonWidget = Cast<UCommonActivatableWidget>(Widget))
    {
        CommonWidget->ActivateWidget();
    }

    APlayerController* PlayerController = GetOwningPlayer();
    if (!PlayerController && GetWorld())
    {
        PlayerController = GetWorld()->GetFirstPlayerController();
    }

    if (PlayerController)
    {
        FInputModeUIOnly InputMode;
        InputMode.SetWidgetToFocus(Widget->TakeWidget());
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PlayerController->SetInputMode(InputMode);
        PlayerController->bShowMouseCursor = true;
    }
}

const FWorldForgeSaveSlotSummary* UWorldForgeSaveSelectWidget::FindSummaryBySlotIndex(const int32 SlotIndex) const
{
    return SaveSlotSummaries.FindByPredicate(
        [SlotIndex](const FWorldForgeSaveSlotSummary& Summary)
        {
            return Summary.SlotIndex == SlotIndex;
        });
}

void UWorldForgeSaveSelectWidget::SetDetailText(UTextBlock* TextBlock, const FString& Value) const
{
    if (TextBlock)
    {
        TextBlock->SetText(FText::FromString(Value));
    }
}

void UWorldForgeSaveSelectWidget::SetStatusText(const FString& Value) const
{
    if (TXT_SaveSelectStatus)
    {
        TXT_SaveSelectStatus->SetText(FText::FromString(Value));
    }
}

FString UWorldForgeSaveSelectWidget::MakeSlotButtonLabel(const FWorldForgeSaveSlotSummary& Summary) const
{
    if (!Summary.bHasSave)
    {
        return TEXT("Empty Slot");
    }

    return FString::Printf(TEXT("%s\n%s"), *Summary.WorldName, *Summary.SaveSlotName);
}

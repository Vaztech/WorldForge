#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "Save/SaveSubsystem.h"
#include "WorldForgeSaveSelectWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UUserWidget;
class UWorldForgeSaveSlotButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWorldForgeSaveSlotButtonClicked, UWorldForgeSaveSlotButton*, Button, int32, SlotIndex);

/** Native button used for dynamically generated save-slot rows. */
UCLASS()
class WORLDFORGE_API UWorldForgeSaveSlotButton : public UButton
{
    GENERATED_BODY()

public:
    void InitializeSlot(const FWorldForgeSaveSlotSummary& InSummary);

    UFUNCTION()
    void HandleClicked();

    FWorldForgeSaveSlotButtonClicked OnSaveSlotClicked;

    UPROPERTY()
    FWorldForgeSaveSlotSummary Summary;

    UPROPERTY()
    int32 SlotIndex = INDEX_NONE;

    UPROPERTY()
    FString SaveSlotName;
};

/**
 * C++ bridge for WBP_WF_SaveSelect.
 *
 * The Blueprint owns the shell layout. This class populates the 8 fixed save
 * slots from USaveSubsystem, keeps Load as a placeholder, and deletes only the
 * selected fixed save slot.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldForgeSaveSelectWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "WorldForge|SaveSelect|Flow")
    void SetPreviousWidget(UUserWidget* InWidget);

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidgetOptional))
    UPanelWidget* PNL_SaveSlotList = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_DetailWorldName = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_DetailLastPlayed = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_DetailColonyName = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_DetailDayYear = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_DetailSaveVersion = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TXT_SaveSelectStatus = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* BTN_LoadSelected = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* BTN_DeleteSelected = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* BTN_Back = nullptr;

private:
    UFUNCTION()
    void HandleSlotButtonClicked(UWorldForgeSaveSlotButton* Button, int32 SlotIndex);

    UFUNCTION()
    void HandleLoadSelectedClicked();

    UFUNCTION()
    void HandleDeleteSelectedClicked();

    UFUNCTION()
    void HandleBackClicked();

    void BindControls();
    void RefreshSaveSlots();
    void RebuildSlotList();
    void SelectSlotByIndex(int32 SlotIndex);
    void UpdateDetailsForSelectedSlot();
    void RestorePreviousWidget();
    const FWorldForgeSaveSlotSummary* FindSummaryBySlotIndex(int32 SlotIndex) const;
    void SetDetailText(UTextBlock* TextBlock, const FString& Value) const;
    void SetStatusText(const FString& Value) const;
    FString MakeSlotButtonLabel(const FWorldForgeSaveSlotSummary& Summary) const;

    UPROPERTY()
    TArray<FWorldForgeSaveSlotSummary> SaveSlotSummaries;

    UPROPERTY()
    TWeakObjectPtr<UUserWidget> PreviousWidget;

    int32 SelectedSlotIndex = INDEX_NONE;
};

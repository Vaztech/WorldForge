#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/SlateEnums.h"
#include "UObject/SoftObjectPtr.h"
#include "WorldForgeSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UPanelWidget;
class USlider;
class USoundClass;
class USoundMix;
class UTextBlock;
class UUserWidget;

USTRUCT(BlueprintType)
struct FWorldForgeSettingsState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bAutosaveEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bTutorialHintsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bPauseOnMenuEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float MasterVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float MusicVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float SfxVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float AmbienceVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bVSyncEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	FString ResolutionLabel = TEXT("1920x1080");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	FString WindowModeLabel = TEXT("Borderless");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float TextScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bHighContrastMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bColorblindAssist = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bReduceMotion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	bool bLargeTextMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Settings")
	float AccessibilityTextScale = 1.0f;
};

/**
 * C++ bridge for WBP_WF_Settings.
 *
 * The Blueprint owns the existing layout while this class wires the Back button
 * so the Settings screen can return to the Main Menu while a lightweight settings
 * state is tracked and persisted independently from world save slots.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldForgeSettingsWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Settings|Flow")
	void SetPreviousWidget(UUserWidget* InWidget);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Back = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Apply = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Reset = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_AutosavePlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_TutorialHintsPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_PauseOnMenuPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_VSyncPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	USlider* SLD_MasterVolume = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	USlider* SLD_MusicVolume = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	USlider* SLD_SFXVolume = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	USlider* SLD_AmbienceVolume = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	USlider* SLD_TextSize = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ResolutionPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UComboBoxString* ResolutionDropdown = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_WindowModePlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UComboBoxString* WindowModeDropdown = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* VB_ControlsSection = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_KeybindsPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* VB_AccessibilitySection = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColorblindModePlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_ReduceFlashingPlaceholder = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_HighContrastMode = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_ColorblindAssist = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_ReduceMotion = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCheckBox* CHK_LargeTextMode = nullptr;

private:
	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleApplyClicked();

	UFUNCTION()
	void HandleResetClicked();

	UFUNCTION()
	void HandleAutosaveChanged(bool bIsChecked);

	UFUNCTION()
	void HandleTutorialHintsChanged(bool bIsChecked);

	UFUNCTION()
	void HandlePauseOnMenuChanged(bool bIsChecked);

	UFUNCTION()
	void HandleVSyncChanged(bool bIsChecked);

	UFUNCTION()
	void HandleMasterVolumeChanged(float InValue);

	UFUNCTION()
	void HandleMusicVolumeChanged(float InValue);

	UFUNCTION()
	void HandleSfxVolumeChanged(float InValue);

	UFUNCTION()
	void HandleAmbienceVolumeChanged(float InValue);

	UFUNCTION()
	void HandleTextScaleChanged(float InValue);

	UFUNCTION()
	void HandleHighContrastChanged(bool bIsChecked);

	UFUNCTION()
	void HandleColorblindAssistChanged(bool bIsChecked);

	UFUNCTION()
	void HandleReduceMotionChanged(bool bIsChecked);

	UFUNCTION()
	void HandleLargeTextModeChanged(bool bIsChecked);

	UFUNCTION()
	void HandleRebindPlaceholderClicked();

	UFUNCTION()
	void HandleResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleWindowModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	void RestorePreviousWidget();
	void BindSettingsWidgets();
	void InitializeSettingsState();
	void LoadPersistedSettings();
	void SavePersistedSettings() const;
	void EnsureResolutionDropdown();
	void EnsureControlsFoundationWidgets();
	void EnsureAccessibilityFoundationWidgets();
	void ApplyAudioSettings(const FWorldForgeSettingsState& SettingsState);
	void ApplyAccessibilitySettings(const FWorldForgeSettingsState& SettingsState) const;
	void RefreshUIFromPendingSettings();
	void LogMissingSettingsWidgets() const;
	FWorldForgeSettingsState MakeDefaultSettings() const;
	FString DescribeSettingsState(const FWorldForgeSettingsState& SettingsState) const;
	UCheckBox* GetReduceMotionCheckbox() const;

	UPROPERTY(EditDefaultsOnly, Category = "WorldForge|Settings|Audio")
	TSoftObjectPtr<USoundMix> RuntimeSoundMix;

	UPROPERTY(EditDefaultsOnly, Category = "WorldForge|Settings|Audio")
	TSoftObjectPtr<USoundClass> RuntimeMasterSoundClass;

	UPROPERTY(EditDefaultsOnly, Category = "WorldForge|Settings|Audio")
	TSoftObjectPtr<USoundClass> RuntimeMusicSoundClass;

	UPROPERTY(EditDefaultsOnly, Category = "WorldForge|Settings|Audio")
	TSoftObjectPtr<USoundClass> RuntimeSfxSoundClass;

	UPROPERTY(EditDefaultsOnly, Category = "WorldForge|Settings|Audio")
	TSoftObjectPtr<USoundClass> RuntimeAmbienceSoundClass;

	UPROPERTY()
	TWeakObjectPtr<UUserWidget> PreviousWidget;

	UPROPERTY(Transient)
	FWorldForgeSettingsState CurrentSettings;

	UPROPERTY(Transient)
	FWorldForgeSettingsState PendingSettings;

	UPROPERTY(Transient)
	TWeakObjectPtr<USoundMix> ActiveRuntimeSoundMix;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> RebindPlaceholderButtons;

	bool bIsRefreshingUI = false;
	bool bControlsFoundationCreated = false;
	bool bAccessibilityFoundationCreated = false;
};

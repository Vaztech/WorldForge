#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "Types/SlateEnums.h"
#include "WorldForgeEmbarkSetupWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class UImage;
class UPaperFlipbook;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UUserWidget;
class UWidget;
class UWorldForgeEmbarkationManager;
class UWorldForgeEmbarkSetupWidget;
struct FWorldForgeEmbarkColonist;

UCLASS()
class WORLDFORGE_API UWorldForgeEmbarkColonistRowClickProxy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorldForgeEmbarkSetupWidget* InOwnerWidget, int32 InColonistIndex);

	UFUNCTION()
	void HandleClicked();

private:
	TWeakObjectPtr<UWorldForgeEmbarkSetupWidget> OwnerWidget;
	int32 ColonistIndex = INDEX_NONE;
};

/**
 * C++ bridge for the CommonUI embark setup screen.
 *
 * WBP_WF_WorldConfig owns the layout, while this parent class keeps the basic
 * UMG field wiring in C++ so the Blueprint graph can stay simple and avoid
 * fragile component-bound text box, combo box, and button events.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldForgeEmbarkSetupWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/**
	 * Called by the widget that opened this screen (e.g. WBP_WF_MainMenu) so that
	 * Back can restore it.  Mirrors SetSetupWidgetToRestore on the Loadout widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Flow")
	void SetPreviousWidget(UUserWidget* InWidget);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|Embark|Flow")
	TSubclassOf<UUserWidget> EmbarkLoadoutWidgetClass;

	UPROPERTY(meta = (BindWidgetOptional))
	UEditableTextBox* WorldNameInput = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UEditableTextBox* SeedInput = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_RandomizeSeed = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UComboBoxString* WorldSizeDropdown = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_TabWorld = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_TabColony = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_TabColonists = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_TabWorld = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_TabColony = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_TabColonists = nullptr;

	// Existing content section panels toggled by tab switching
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_WorldSetup = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_ColonyMode = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_RaceSelection = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_RaceDetails = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* PNL_StartingColonists = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistListHeader = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* PNL_ColonistList = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* VB_ColonistList = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_ColonistPlaceholder_01 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_ColonistPlaceholder_02 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_ColonistPlaceholder_03 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistPlaceholder_01 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistPlaceholder_02 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistPlaceholder_03 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistName = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistRace = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistAge = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistBackstory = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistSkills = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_ColonistTraits = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_RerollSelectedColonist = nullptr;

	// Empty tab content overlay (hidden since content lives in section panels)
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* SB_TabContentArea = nullptr;

	// Tab button row container (reordered to top at construct time)
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* HB_EmbarkTabs = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_MixedColony = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_SingleRaceColony = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Back = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Continue = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* NextButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UScrollBox* PNL_RaceList = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceListHeader = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_RacePlaceholder_Dwarf = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_RacePlaceholder_Human = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_RacePlaceholder_HighElf = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_ConstructFolk = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_Namekian = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_FaeKin = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_Tidecaller = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_Oni = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Race_DarkElf = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceName = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceDescription = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceStrengths = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceWeaknesses = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RaceTraits = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UImage* IMG_RacePreview = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RacePreviewStatus = nullptr;

private:
	enum class EEmbarkSetupTab : uint8
	{
		World,
		Colony,
		Colonists
	};

	UFUNCTION()
	void HandleWorldNameTextChanged(const FText& InText);

	UFUNCTION()
	void HandleWorldNameTextCommitted(const FText& InText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleSeedTextChanged(const FText& InText);

	UFUNCTION()
	void HandleSeedTextCommitted(const FText& InText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleRandomizeSeedClicked();

	UFUNCTION()
	void HandleWorldSizeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleTabWorldClicked();

	UFUNCTION()
	void HandleTabColonyClicked();

	UFUNCTION()
	void HandleTabColonistsClicked();

	UFUNCTION()
	void HandleMixedColonyClicked();

	UFUNCTION()
	void HandleSingleRaceColonyClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleRaceDwarfClicked();

	UFUNCTION()
	void HandleRaceHighElfClicked();

	UFUNCTION()
	void HandleRaceConstructFolkClicked();

	UFUNCTION()
	void HandleRaceNamekianClicked();

	UFUNCTION()
	void HandleRaceFaeKinClicked();

	UFUNCTION()
	void HandleRaceTidecallerClicked();

	UFUNCTION()
	void HandleRaceOniClicked();

	UFUNCTION()
	void HandleRaceDarkElfClicked();

	UFUNCTION()
	void HandleColonist01Clicked();

	UFUNCTION()
	void HandleColonist02Clicked();

	UFUNCTION()
	void HandleColonist03Clicked();

	UFUNCTION()
	void HandleRerollSelectedColonistClicked();

	UWorldForgeEmbarkationManager* GetEmbarkationManager() const;
	void BindControls();
	void LogBindingStatus() const;
	void DisableLegacyNextButton();
	void ApplyInputAndLayoutStyle();
	void WrapRaceDetailsInFixedSizeBox();
	void ApplyStableTabLayout();
	void NormalizeInitialSeedDisplay();
	void ReorderTabsToTop();
	void SetActiveEmbarkTab(EEmbarkSetupTab ActiveTab);
	void InitializeRaceDisplay();
	void ConfigureRaceButton(UButton* Button, const FText& Label, bool bEnabled);
	void OpenColonistsTab();
	UPanelWidget* GetColonistListPanel() const;
	void RebuildColonistList(const TArray<FWorldForgeEmbarkColonist>& Colonists);
	UButton* CreateColonistRowButton(int32 ColonistIndex, const FWorldForgeEmbarkColonist& Colonist);
	void RefreshColonistButtons();
	void RefreshColonistButton(int32 ColonistIndex);
	void SelectStartingColonist(int32 ColonistIndex);
	void RefreshSelectedColonistDetails();
	void SelectRaceByName(const TCHAR* RaceName);
	void SelectRaceByTag(FGameplayTag RaceTag);
	void UpdateRaceDetails(FGameplayTag RaceTag);
	void UpdateRacePreviewForTag(FGameplayTag RaceTag);
	bool ResolveIdleFlipbookForRace(FGameplayTag RaceTag, UPaperFlipbook*& OutFlipbook) const;
	bool ApplyStaticFlipbookPreview(UPaperFlipbook* Flipbook);
	void ApplyWorldNameText(const FText& InText);
	void ApplySeedText(const FText& InText);
	FString SanitizeSeedInput(const FString& RawSeed) const;
	void ApplyWorldSizeSelection(const FString& SelectedItem);
	void CommitCurrentFieldsToManager();
	TSubclassOf<UUserWidget> ResolveEmbarkLoadoutWidgetClass() const;
	bool OpenEmbarkLoadout();
	void RestorePreviousWidget();

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveEmbarkLoadoutWidget;

	UPROPERTY()
	TWeakObjectPtr<UUserWidget> PreviousWidget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWorldForgeEmbarkColonistRowClickProxy>> ColonistRowClickProxies;

	bool bApplyingSanitizedSeedText = false;
	int32 SelectedColonistIndex = INDEX_NONE;

	friend class UWorldForgeEmbarkColonistRowClickProxy;
};

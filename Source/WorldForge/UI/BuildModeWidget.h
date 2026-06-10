#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "GameplayTagContainer.h"
#include "Player/BuildModeTypes.h"
#include "Mining/MiningTypes.h"
#include "Grid/LayerViewTypes.h"
#include "Data/WorldForgeBuildDataTypes.h"
#include "BuildModeWidget.generated.h"

class UBuildModeManager;
class UWorldForgeDataRegistry;
class UWorldForgeBuildPrefabDataSubsystem;
class UStaticMesh;
class UCanvasPanel;
class UHorizontalBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;

USTRUCT(BlueprintType)
struct FBuildModeFarmSeedSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldForge|BuildUI|Farming")
	FName SeedID = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "WorldForge|BuildUI|Farming")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "WorldForge|BuildUI|Farming")
	int32 Quantity = 0;
};

enum class EBuildModeOptionButtonKind : uint8
{
	BuildPiece,
	Prefab,
	FarmSeed
};

class UBuildModeWidget;

UCLASS()
class WORLDFORGE_API UBuildModeOptionButton : public UButton
{
	GENERATED_BODY()

public:
	void InitializeOption(UBuildModeWidget* InOwner, EBuildModeOptionButtonKind InKind, FName InOptionID);

private:
	TWeakObjectPtr<UBuildModeWidget> OwnerWidget;
	EBuildModeOptionButtonKind OptionKind = EBuildModeOptionButtonKind::Prefab;
	FName OptionID = NAME_None;

	UFUNCTION()
	void HandleClicked();
};

UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UBuildModeWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ================================
	// Actions (call from Blueprint buttons)
	// ================================

	// Player selected a prefab building from the building list.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SelectPrefabBuilding(FGameplayTag BuildingTag);

	// Player selected a material for custom drawing.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SelectMaterial(FGameplayTag MaterialTag);

	// Player switched to custom draw mode.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SwitchToCustomDrawMode();

	// Player switched to dig mode.
	// Bind your new Dig button to this function.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SwitchToDigMode();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SwitchToMineMode();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SwitchToFarmMode();

	// Player clicked cancel in the UI.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void CancelBuild();

	// Player clicked confirm in the UI.
	// Custom draw uses this to commit selected cells.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void ConfirmBuild();

	// Player toggled the static terrain grid overlay visibility.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Grid")
	void ToggleStaticGridOverlay();

	// Player/UI explicitly enabled or disabled the static terrain grid overlay.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Grid")
	void SetStaticGridOverlayEnabled(bool bEnabled);

	// Returns whether the static terrain grid overlay is currently enabled.
	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Grid")
	bool IsStaticGridOverlayEnabled() const;

	// Player toggled mining removal behavior between exact voxel cube and SDF brush.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Mining")
	void ToggleMiningRemovalMode();

	// Player/UI explicitly selected mining removal behavior.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Mining")
	void SetMiningRemovalMode(EMiningRemovalMode NewMode);

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	EMiningRemovalMode GetMiningRemovalMode() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	FText GetMiningRemovalModeLabel() const;

	// Player toggled mine selection height between default 2-block column and 1 voxel.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Mining")
	void ToggleMiningHeightMode();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Mining")
	void SetMiningHeightMode(EWFMiningHeightMode NewMode);

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	EWFMiningHeightMode GetMiningHeightMode() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	FText GetMiningHeightModeLabel() const;

	// ================================
	// Read-only data accessors for Blueprint UI
	// ================================

	// Registry-driven building list for prefab buttons.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	TArray<FGameplayTag> GetAvailableBuildingTags() const;

	// Initial deterministic material list for custom draw UI.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	TArray<FGameplayTag> GetAvailableMaterialTags() const;

	// DataTable-driven category summaries for read-only build menu display.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	TArray<FString> GetBuildDataCategorySummaries() const;

	// DataTable-driven build piece summaries. Pass None to include every category.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	TArray<FString> GetBuildDataBuildPieceSummaries(FName Category) const;

	// DataTable-driven prefab summaries. Pass None to include every category.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	TArray<FString> GetBuildDataPrefabSummaries(FName Category) const;

	// Read-only seed summaries for the farm panel. SeedID is the crop row name used by farming.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Farming")
	TArray<FBuildModeFarmSeedSummary> GetAvailableFarmSeedSummaries() const;

	// Selects a currently owned seed for the next farm zone without consuming inventory.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Farming")
	void SelectFarmSeedFromInventory(FName SeedID);

	void HandleRuntimeOptionClicked(EBuildModeOptionButtonKind Kind, FName OptionID);

	// Logs the current DataTable counts and a few sample rows from the build menu UI.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void LogBuildPrefabDataTableSummary() const;

	// Log-only selection handlers for future DataTable-driven UI buttons.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void LogSelectedBuildPieceID(FName BuildID) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void LogSelectedBuildPrefabID(FName PrefabID) const;

	// Stores a validated DataTable build piece selection without entering placement mode.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void SelectBuildPieceFromDataTable(FName BuildID);

	// Stores a validated DataTable prefab selection without entering placement mode.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void SelectBuildingPrefabFromDataTable(FName PrefabID);

	// Clears any DataTable-driven build piece/prefab UI selection.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI|Data")
	void ClearDataTableBuildSelection();

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	FName GetSelectedBuildPieceID() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	FName GetSelectedBuildingPrefabID() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	bool HasSelectedBuildPiece() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	bool HasSelectedBuildingPrefab() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	bool GetSelectedBuildPiecePreviewMesh(UStaticMesh*& OutMesh) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	bool GetSelectedBuildingPrefabPreviewMesh(UStaticMesh*& OutMesh) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Data")
	bool GetSelectedBuildDataPreviewMesh(UStaticMesh*& OutMesh) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetBuildingDisplayName(FGameplayTag BuildingTag) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetItemDisplayName(FGameplayTag ItemTag) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetMaterialDisplayName(FGameplayTag MaterialTag) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetSelectedBuildingDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetSelectedMaterialDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetCurrentCostLabel() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetCurrentAvailabilityLabel() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	bool GetCanAffordCurrentSelection() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	int32 GetCurrentCellCount() const;

	// CUBE-MINING-V3-FLOOD: separated mine-plan counts for the UI. GetCurrentCellCount
	// above is the current active layer only; these expose the full multi-layer plan so
	// the widget can stop showing one ambiguous number.

	/** Total cells in the full persistent multi-layer mine plan (what Confirm commits). */
	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	int32 GetTotalMinePlanCount() const;

	/** Number of distinct voxel layers the mine plan spans. */
	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	int32 GetMinePlanLayerCount() const;

	/**
	 * Ready-to-display summary the Blueprint can bind straight to a text block, e.g.
	 * "Current layer: 9  •  Plan: 30 across 3 layers". Returns the plain current-layer
	 * count when not in Mine mode, so a single text block can serve all tools.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI|Mining")
	FText GetMinePlanSummaryText() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	EBuildToolMode GetCurrentToolMode() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	FText GetCurrentToolModeLabel() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	bool IsDigModeActive() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	bool IsMineModeActive() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|BuildUI")
	bool IsFarmModeActive() const;

	// ================================
	// Events (implement in Blueprint to update visuals)
	// ================================

	// Called when build mode changes (None / Prefab / CustomDraw).
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnBuildModeChanged(EBuildMode NewMode);

	// Called when stockpile amounts change for the selected material.
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnStockpileChanged(FGameplayTag MaterialTag, int32 Available);

	// Called when cell count changes in custom draw mode.
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnCellCountChanged(int32 CellCount);

	// Called when the hovered grid cell changes.
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnHoveredCellChanged(FIntPoint Cell);

	// Called when the selected building/material or active mode changes.
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnSelectionChanged(EBuildMode NewMode, FGameplayTag BuildingTag, FGameplayTag MaterialTag);

	// Called when build actions produce player-facing feedback.
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnBuildFeedbackMessage(const FString& Message);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> CanvasPanel_0 = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> HouseButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> FreeBuildButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Dig = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Confirm = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Cancel = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_ToggleGrid = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Dig = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Cancel = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Mode = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Building = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Material = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_HoveredCell = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_CellCount = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_Status = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CB_ModeSelect = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SizeBox_0 = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> BottomToolbar = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_Build = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_Mine = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_Farm = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_OpenPrefabs = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_OpenBuildPieces = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BTN_OpenFreeBuild = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Panel_BuildPopup = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Panel_BuildPieceList = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ScrollBox_BuildPieceOptions = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Panel_PrefabList = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ScrollBox_PrefabOptions = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Panel_FarmSeeds = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text_FarmSeedsHeader = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ScrollBox_SeedOptions = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text_NoSeedsAvailable = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text_StatusToast = nullptr;

	UPROPERTY(Transient)
	bool bRuntimeBuildModeLayoutCreated = false;

	void BuildCompactRuntimeLayout();
	void HideLegacyPlayerFacingWidgets();
	void HideRuntimeWidget(UWidget* Widget) const;
	UTextBlock* CreateRuntimeTextBlock(FName Name, const FText& Text, int32 FontSize);
	UButton* CreateRuntimeTextButton(FName Name, const FText& Text, int32 FontSize);
	void AddToolbarEntry(UWidget* Widget, const FVector2D& Size);
	void AddSizedVerticalEntry(UVerticalBox* Parent, UWidget* Widget, const FVector2D& Size, const FMargin& Padding);
	void CreateBuildPopup(UCanvasPanel* RootCanvas);
	void CreateBuildPiecePanel(UCanvasPanel* RootCanvas);
	void CreatePrefabPanel(UCanvasPanel* RootCanvas);
	void CreateFarmSeedsPanel(UCanvasPanel* RootCanvas);
	void CreateStatusToast(UCanvasPanel* RootCanvas);
	void CollapseTransientPanels();
	void ShowStatusToast(const FText& Message);
	void RefreshBuildPieceList();
	void RefreshPrefabList();
	void RefreshFarmSeedPanel();
	FName ParseBuildPieceIDFromSummary(const FString& Summary) const;
	FName ParsePrefabIDFromSummary(const FString& Summary) const;
	FText MakeFarmSeedDisplayName(FName SeedID) const;
	UWidget* CreateBuildPieceCardContent(const FWorldForgeBuildPieceRow& Row);
	UWidget* CreatePrefabCardContent(const FWorldForgeBuildingPrefabRow& Row);

	UFUNCTION()
	void HandleBuildClicked();

	UFUNCTION()
	void HandleOpenBuildPiecesClicked();

	UFUNCTION()
	void HandleOpenPrefabsClicked();

	UFUNCTION()
	void HandleOpenFreeBuildClicked();

	UFUNCTION()
	void HandleFarmClicked();

	UFUNCTION()
	void HandleDigChromeClicked();

	UFUNCTION()
	void HandleMineChromeClicked();

	UFUNCTION()
	void HandleConfirmChromeClicked();

	UFUNCTION()
	void HandleCancelChromeClicked();

	UFUNCTION()
	void HandleNativeBuildFeedbackMessage(const FString& Message);

	UPROPERTY()
	UBuildModeManager* CachedManager = nullptr;

	UPROPERTY(Transient)
	FName SelectedBuildPieceID = NAME_None;

	UPROPERTY(Transient)
	FName SelectedBuildingPrefabID = NAME_None;

	UPROPERTY(Transient)
	bool bHasSelectedBuildPiece = false;

	UPROPERTY(Transient)
	bool bHasSelectedBuildingPrefab = false;

	UBuildModeManager* GetManager();

	UWorldForgeDataRegistry* GetRegistry() const;

	UWorldForgeBuildPrefabDataSubsystem* GetBuildDataSubsystem() const;
};

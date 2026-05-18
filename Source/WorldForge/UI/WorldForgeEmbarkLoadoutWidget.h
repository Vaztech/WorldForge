#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/SlateEnums.h"
#include "Embark/EmbarkLoadoutTypes.h"
#include "WorldForgeEmbarkLoadoutWidget.generated.h"

class UButton;
class UComboBoxString;
class UHorizontalBox;
class UScrollBox;
class UTextBlock;
class UUserWidget;
class UVerticalBox;
class UWorldForgeEmbarkLoadoutWidget;
class UWorldForgeEmbarkationManager;

UCLASS()
class WORLDFORGE_API UWorldForgeEmbarkLoadoutSummaryAction : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorldForgeEmbarkLoadoutWidget* InOwner, FName InItemId, int32 InAction);

    UFUNCTION()
    void HandleClicked();

private:
    UPROPERTY(Transient)
    TObjectPtr<UWorldForgeEmbarkLoadoutWidget> Owner = nullptr;

    FName ItemId = NAME_None;
    int32 Action = 0;
};

/**
 * C++ bridge for WBP_WF_EmbarkLoadout.
 *
 * The Widget Blueprint owns the layout. This class binds the existing +/- row
 * buttons and summary text blocks to the simple embark-loadout state stored in
 * UWorldForgeEmbarkationManager.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldForgeEmbarkLoadoutWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	void SetSetupWidgetToRestore(UUserWidget* InWidget);
	void HandleSummaryAction(FName ItemId, int32 Action);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UHorizontalBox* HB_LoadoutMain = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* PNL_LoadoutCategories = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* PNL_LoadoutItems = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* PNL_LoadoutSummary = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* VB_LoadoutCategories = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryFood = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryTools = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryMaterials = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategorySeeds = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryMedicine = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryLivestock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategoryWeapons = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CategorySpecial = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_FoodRations_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_FoodRations_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_FoodRations_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_FoodRations_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_FoodRations_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Wood_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Wood_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Wood_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Wood_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Wood_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Stone_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Stone_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Stone_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Stone_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Stone_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UComboBoxString* SeedTypeDropdown = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Seeds_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Seeds_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Seeds_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Seeds_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Seeds_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Pickaxe_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Pickaxe_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Pickaxe_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Pickaxe_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Pickaxe_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Medicine_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Medicine_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Medicine_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Medicine_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Medicine_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Cloth_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Cloth_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Cloth_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Cloth_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_Cloth_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CopperIngots_Minus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_CopperIngots_Plus = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_CopperIngots_Qty = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_CopperIngots_Cost = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_CopperIngots_Total = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_EmbarkPoints = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_UsedPoints = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_RemainingPoints = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_SelectedItemSummary = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* VB_LoadoutSummary = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* TXT_LoadoutWarning = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Back = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Continue = nullptr;

private:
	UFUNCTION()
	void HandleFoodRationsMinusClicked();

	UFUNCTION()
	void HandleFoodRationsPlusClicked();

	UFUNCTION()
	void HandleWoodMinusClicked();

	UFUNCTION()
	void HandleWoodPlusClicked();

	UFUNCTION()
	void HandleStoneMinusClicked();

	UFUNCTION()
	void HandleStonePlusClicked();

	UFUNCTION()
	void HandleSeedsMinusClicked();

	UFUNCTION()
	void HandleSeedsPlusClicked();

	UFUNCTION()
	void HandlePickaxeMinusClicked();

	UFUNCTION()
	void HandlePickaxePlusClicked();

	UFUNCTION()
	void HandleMedicineMinusClicked();

	UFUNCTION()
	void HandleMedicinePlusClicked();

	UFUNCTION()
	void HandleClothMinusClicked();

	UFUNCTION()
	void HandleClothPlusClicked();

	UFUNCTION()
	void HandleCopperIngotsMinusClicked();

	UFUNCTION()
	void HandleCopperIngotsPlusClicked();

	UFUNCTION()
	void HandleSeedTypeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleContinueClicked();

	UWorldForgeEmbarkationManager* GetEmbarkationManager() const;
	void BindControls();
	void ApplyRuntimeLayoutPolish();
	void LogBindingStatus() const;
	void PopulateSeedTypeDropdown();
	FName GetActiveSeedItemId() const;
	void AddItem(FName ItemId);
	void RemoveItem(FName ItemId);
	void UpdateLoadoutDisplay(const FText& WarningText = FText::GetEmpty());
	void EnsureSelectedLoadoutSummaryPanel();
	void RebuildSelectedLoadoutSummary();
	void AddSelectedLoadoutSummaryRow(const FEmbarkSelectedItem& Item, const FText& DisplayName, int32 TotalCost);
	void UpdateItemText(FName ItemId, UTextBlock* QuantityText, UTextBlock* CostText, UTextBlock* TotalText) const;
	void RestoreSetupWidget();

	// Current selected seed type from DT_LowPoly
	FString ActiveSeedDisplayName;
	FName ActiveSeedItemId;

	// Maps dropdown display name -> item ID (Seed.CropName)
	TMap<FString, FName> SeedDisplayNameToItemId;


	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> SelectedLoadoutItemsScrollBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SelectedLoadoutItemsPanel = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWorldForgeEmbarkLoadoutSummaryAction>> SummaryActionHandlers;

	int32 SummaryRebuildSerial = 0;

	UPROPERTY()
	TWeakObjectPtr<UUserWidget> SetupWidgetToRestore;
};

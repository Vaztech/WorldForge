#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Player/BuildModeTypes.h"
#include "BuildModeWidget.generated.h"

class UBuildModeManager;
class UWorldForgeDataRegistry;

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

	// ================================
	// Read-only data accessors for Blueprint UI
	// ================================

	// Registry-driven building list for prefab buttons.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	TArray<FGameplayTag> GetAvailableBuildingTags() const;

	// Initial deterministic material list for custom draw UI.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	TArray<FGameplayTag> GetAvailableMaterialTags() const;

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

	UPROPERTY()
	UBuildModeManager* CachedManager = nullptr;

	UBuildModeManager* GetManager();

	UWorldForgeDataRegistry* GetRegistry() const;
};
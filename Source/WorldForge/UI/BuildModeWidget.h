// Source/WorldForge/UI/BuildModeWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Player/BuildModeTypes.h"
#include "BuildModeWidget.generated.h"

class UBuildModeManager;

UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UBuildModeWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ================================
	// Actions (call from Blueprint buttons)
	// ================================

	// Player selected a prefab building from the building list
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SelectPrefabBuilding(FGameplayTag BuildingTag);

	// Player selected a material for custom drawing
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SelectMaterial(FGameplayTag MaterialTag);

	// Player switched to custom draw mode
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void SwitchToCustomDrawMode();

	// Player clicked cancel in the UI
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void CancelBuild();

	// Player clicked confirm in the UI (custom draw only)
	UFUNCTION(BlueprintCallable, Category = "WorldForge|BuildUI")
	void ConfirmBuild();

	// ================================
	// Events (implement in Blueprint to update visuals)
	// ================================

	// Called when build mode changes (None / Prefab / CustomDraw)
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnBuildModeChanged(EBuildMode NewMode);

	// Called when stockpile amounts change for the selected material
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnStockpileChanged(FGameplayTag MaterialTag, int32 Available);

	// Called when cell count changes in custom draw mode
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnCellCountChanged(int32 CellCount);

	// Called when the hovered grid cell changes
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnHoveredCellChanged(FIntPoint Cell);

	// Called when the selected building/material or active mode changes
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnSelectionChanged(EBuildMode NewMode, FGameplayTag BuildingTag, FGameplayTag MaterialTag);

	// Called when build actions produce player-facing feedback
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|BuildUI")
	void OnBuildFeedbackMessage(const FString& Message);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:

	UPROPERTY()
	UBuildModeManager* CachedManager = nullptr;

	UBuildModeManager* GetManager();
};
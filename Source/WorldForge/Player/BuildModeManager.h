#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BuildModeTypes.h"
#include "WorldForgeDataTypes.h"
#include "BuildModeManager.generated.h"

class ABuildGhostActor;
class APlayerController;
class UPlayerBuildContext;
class UVoxelWorldManager;
class UVoxelConstructionSystem;
class USettlementManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildModeChanged, EBuildMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCellCountChanged, int32, CellCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoveredCellChanged, FIntPoint, HoveredCell);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBuildSelectionChanged, EBuildMode, NewMode, FGameplayTag, BuildingTag, FGameplayTag, MaterialTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildFeedbackMessage, const FString&, Message);

UCLASS(BlueprintType)
class WORLDFORGE_API UBuildModeManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ================================
	// State Queries
	// ================================

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	EBuildMode GetCurrentMode() const { return CurrentMode; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	bool IsInBuildMode() const { return CurrentMode != EBuildMode::None; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	FIntPoint GetHoveredCell() const { return CurrentHoveredCell; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	int32 GetSelectedCellCount() const { return SelectedCells.Num(); }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	FGameplayTag GetSelectedBuildingTag() const { return SelectedBuildingTag; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	FGameplayTag GetSelectedMaterialTag() const { return SelectedMaterialTag; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	int32 GetRotationIndex() const { return RotationIndex; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	bool HasValidHoveredCell() const { return bHoveredCellValid; }

	// ================================
	// Live Cost / Availability Queries
	// ================================

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Cost")
	FGameplayTag GetCurrentRequiredItemTag() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Cost")
	int32 GetCurrentRequiredUnits() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Cost")
	int32 GetCurrentAvailableUnits() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Cost")
	bool CanAffordCurrentSelection() const;

	// ================================
	// Mode Entry / Exit
	// ================================

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void EnterPrefabMode(FGameplayTag BuildingTag);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void EnterCustomDrawMode(FGameplayTag MaterialTag);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void ExitBuildMode();

	// ================================
	// Input Handlers (called by PlayerController)
	// ================================

	// Left click: place prefab or add cell in custom draw
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandlePrimaryClick();

	// Right click: exit build mode (camera pan is handled by the camera Blueprint)
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleSecondaryClick();

	// Z: undo last cell in custom draw
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleUndo();

	// R / Shift+R: rotate prefab
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleRotate(bool bClockwise);

	// Enter: commit custom draw build
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleConfirm();

	// Escape: exit build mode
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleCancel();

	// Called each frame by PlayerController while in build mode.
	// Raycasts from mouse to terrain and updates the hovered cell + ghost.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void UpdateFromMouse(APlayerController* PC);

	// ================================
	// Configuration
	// ================================

	// Which settlement builds are committed to.
	// Set this before entering build mode.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void SetActiveSettlement(int32 SettlementID);

	// Agent assigned to construction tasks. -1 = auto-assign.
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void SetBuildAgentID(int32 AgentID);

	// ================================
	// Delegates (for UI updates)
	// ================================

	UPROPERTY(BlueprintAssignable, Category = "WorldForge|Build")
	FOnBuildModeChanged OnBuildModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldForge|Build")
	FOnCellCountChanged OnCellCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldForge|Build")
	FOnHoveredCellChanged OnHoveredCellChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldForge|Build")
	FOnBuildSelectionChanged OnBuildSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "WorldForge|Build")
	FOnBuildFeedbackMessage OnBuildFeedbackMessage;

private:

	// Current state
	EBuildMode CurrentMode = EBuildMode::None;
	FGameplayTag SelectedBuildingTag;
	FGameplayTag SelectedMaterialTag;
	int32 RotationIndex = 0;
	FIntPoint CurrentHoveredCell = FIntPoint::ZeroValue;
	bool bHoveredCellValid = false;
	float LastHitZ = 0.0f;

	// Custom draw selection (mirrors PlayerBuildContext but local for preview)
	TArray<FIntPoint> SelectedCells;

	// Config
	int32 ActiveSettlementID = -1;
	int32 BuildAgentID = -1;

	// Ghost actor
	UPROPERTY()
	ABuildGhostActor* GhostActor = nullptr;

	// Subsystem helpers
	UPlayerBuildContext* GetBuildContext() const;
	UVoxelWorldManager* GetVoxelManager() const;
	UVoxelConstructionSystem* GetConstructionSystem() const;
	USettlementManager* GetSettlementManager() const;

	// Fallback settlement resolver for build commit paths.
	int32 GetEffectiveSettlementID() const;

	// Internal
	bool CommitPrefabBuild();
	bool CommitCustomBuild();
	void SpawnGhost();
	void DestroyGhost();
	void UpdateGhost();

	void BroadcastSelectionChanged();
	void BroadcastFeedback(const FString& Message);

	// Raycast from screen to terrain
	bool RaycastToTerrain(APlayerController* PC, FVector& OutHitLocation) const;
};
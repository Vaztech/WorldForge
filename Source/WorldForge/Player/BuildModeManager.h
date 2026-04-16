#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BuildModeTypes.h"
#include "VoxelWorldManager.h"
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

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandlePrimaryClick();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleSecondaryClick();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleUndo();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleRotate(bool bClockwise);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleConfirm();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void HandleCancel();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void UpdateFromMouse(APlayerController* PC);

	// Batch 2: external drag state from PlayerController press/release
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void SetDragBuildActive(bool bActive);

	// ================================
	// Configuration
	// ================================

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
	void SetActiveSettlement(int32 SettlementID);

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

	EBuildMode CurrentMode = EBuildMode::None;
	FGameplayTag SelectedBuildingTag;
	FGameplayTag SelectedMaterialTag;
	int32 RotationIndex = 0;
	FIntPoint CurrentHoveredCell = FIntPoint::ZeroValue;

	// 3D hover target. Tracked alongside CurrentHoveredCell so existing
	// 2D UI bindings keep working unchanged. Populated by UpdateFromMouse
	// from either terrain-derived Z or support-block face attachment when the
	// cursor is directly over an existing placed voxel block or a pending
	// selected ghost block.
	FIntVector CurrentHoveredVoxel = FIntVector::ZeroValue;

	bool bHoveredCellValid = false;
	float LastHitZ = 0.0f;

	// Batch 1: suppress repeated confirm input immediately after a successful commit
	bool bJustCommitted = false;

	// Batch 2: keep last valid hover so the ghost stays stable when the cursor
	// briefly loses a clean trace hit while moving quickly.
	bool bHasLastValidVoxel = false;
	FIntVector LastValidVoxel = FIntVector::ZeroValue;

	// Batch 2: click-and-drag continuous build support.
	bool bIsDraggingBuild = false;
	FIntVector LastPlacedDragVoxel = FIntVector(INT32_MIN);

	// Batch 2 fix: proper drag anchor state owned by the subsystem.
	bool bHasDragStart = false;
	FIntVector DragStartVoxel = FIntVector::ZeroValue;
	bool bDragStartedFromBlock = false;

	TArray<FIntPoint> SelectedCells;
	TArray<FIntVector> SelectedCells3D;

	int32 ActiveSettlementID = -1;
	int32 BuildAgentID = -1;

	UPROPERTY()
	ABuildGhostActor* GhostActor = nullptr;

	UPlayerBuildContext* GetBuildContext() const;
	UVoxelWorldManager* GetVoxelManager() const;
	UVoxelConstructionSystem* GetConstructionSystem() const;
	USettlementManager* GetSettlementManager() const;

	int32 GetEffectiveSettlementID() const;

	bool CommitPrefabBuild();
	bool CommitCustomBuild();
	void SpawnGhost();
	void DestroyGhost();
	void UpdateGhost();

	void BroadcastSelectionChanged();
	void BroadcastFeedback(const FString& Message);

	bool RaycastToTerrain(
		APlayerController* PC,
		FVector& OutHitLocation,
		bool& bOutHitPlacedBlock,
		FIntVector& OutSupportBlockVoxel,
		FVector& OutHitNormal) const;

	// Terrain validation helpers
	float GetMaxGroundBuildSlopeDegrees() const;
	bool IsBuildableTerrainType(EWorldForgeTerrainType TerrainType) const;
	bool ValidateGroundTerrainPlacement(const FIntVector& Voxel, FString& OutReason) const;
};
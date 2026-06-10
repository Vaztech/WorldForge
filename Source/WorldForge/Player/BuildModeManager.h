#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BuildModeTypes.h"
#include "VoxelWorldManager.h"
#include "WorldForgeDataTypes.h"
#include "Player/DigResult.h"
#include "Mining/MiningTypes.h"
#include "Grid/LayerViewTypes.h"
#include "BuildModeManager.generated.h"

class ABuildGhostActor;
class ADigGhostActor;
class AMiningGhostActor;
class AMiningLayerRenderer;
class ATileGridOverlayActor;
class APlayerController;
class UPlayerBuildContext;
class UVoxelWorldManager;
class UVoxelConstructionSystem;
class USettlementManager;
class UDigSubsystem;
class UMiningSubsystem;
class UFarmingSubsystem;
class UGridSelectionSubsystem;
class ULayerViewSubsystem;
class UTexture2D;
class UStaticMesh;

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
    EBuildToolMode GetCurrentToolMode() const { return CurrentToolMode; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool IsInBuildMode() const
    {
        return CurrentMode != EBuildMode::None || CurrentToolMode != EBuildToolMode::None;
    }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    FIntPoint GetHoveredCell() const { return CurrentHoveredCell; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    FIntVector GetHoveredVoxel() const { return CurrentHoveredVoxel; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    int32 GetSelectedCellCount() const { return SelectedCells.Num(); }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    int32 GetSelectedCell3DCount() const { return SelectedCells3D.Num(); }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    int32 GetFarmPaintedCellCount() const { return FarmPaintedWorldCells.Num(); }

    // CUBE-MINING-V3-FLOOD: expose the multi-layer mine plan to the UI. GetSelectedCellCount
    // above is the CURRENT active layer only; these report the full persistent plan that
    // Confirm actually commits, so the widget can show "Current layer: N / Total plan: M".
    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    int32 GetTotalMinePlanCount() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    int32 GetMinePlanLayerCount() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|TileSelection")
    int32 GetTileSelectionCount() const
    {
        if (CurrentToolMode == EBuildToolMode::Farm)
        {
            return FarmPaintedWorldCells.Num();
        }

        return SelectedCells3D.Num();
    }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    FGameplayTag GetSelectedBuildingTag() const { return SelectedBuildingTag; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    FGameplayTag GetSelectedMaterialTag() const { return SelectedMaterialTag; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    int32 GetRotationIndex() const { return RotationIndex; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool HasValidHoveredCell() const { return bHoveredCellValid; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool IsInDigMode() const { return CurrentToolMode == EBuildToolMode::Dig; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool IsInFarmMode() const { return CurrentToolMode == EBuildToolMode::Farm; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool IsInMineMode() const { return CurrentToolMode == EBuildToolMode::Mine; }

    /**
     * Player-facing selection preference. When enabled, selecting an already
     * selected block/cell removes it from the current selection. When disabled,
     * reselecting an already selected block/cell keeps it selected.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Build|Selection")
    bool bToggleSelectedCellsOnReselect = true;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Selection")
    void SetToggleSelectedCellsOnReselect(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Selection")
    bool GetToggleSelectedCellsOnReselect() const { return bToggleSelectedCellsOnReselect; }

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

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Preview")
    bool BeginDataTablePreviewMode(UStaticMesh* PreviewMesh, FName SourceID, bool bIsPrefab);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Preview")
    bool SetBuildPreviewMesh(UStaticMesh* PreviewMesh);

    // ----- Legacy / Debug Dig Tool Mode -----

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void EnterDigMode();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void ExitDigMode();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void ExecuteDig();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void UndoLastDig();

    // ----- Farm / Mine designation tools -----

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void EnterFarmMode();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void EnterMineMode();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Farming")
    void SetSelectedFarmCropSeed(FName CropRowName);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Farming")
    void ClearSelectedFarmCropSeed();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Farming")
    FName GetSelectedFarmCropSeed() const { return SelectedFarmCropRow; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Farming")
    bool HasSelectedFarmCropSeed() const { return !SelectedFarmCropRow.IsNone(); }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build")
    void ExecuteMineDesignation();

    // ================================
    // Input Handlers
    // Called by PlayerController
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
    // Tool Layer Controls
    // ================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void AdjustActiveToolLayer(int32 Delta);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void ResetActiveToolLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Layer")
    int32 GetActiveToolLayerOffset() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void AdjustActiveViewLayer(int32 Delta);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void ResetActiveViewLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Layer")
    int32 GetActiveViewLayerOffset() const;

    // ================================
    // Static Grid Overlay Controls
    // ================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Overlay")
    void SetStaticBaseGridEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Overlay")
    bool ToggleStaticBaseGrid();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Overlay")
    bool IsStaticBaseGridEnabled() const;

    // ================================
    // Mining Removal Mode Controls
    // ================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    void SetMiningRemovalMode(EMiningRemovalMode NewMode);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    EMiningRemovalMode ToggleMiningRemovalMode();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    EMiningRemovalMode GetMiningRemovalMode() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    FText GetMiningRemovalModeText() const;

    // ================================
    // Mining Height Mode Controls
    // ================================

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    void SetMiningHeightMode(EWFMiningHeightMode NewMode);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    EWFMiningHeightMode ToggleMiningHeightMode();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    EWFMiningHeightMode GetMiningHeightMode() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Mining")
    FText GetMiningHeightModeText() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build|Layer")
    int32 GetActiveVoxelLayerZ() const;

    // ================================
    // Delegates
    // UI listens to these.
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
    EBuildToolMode CurrentToolMode = EBuildToolMode::None;

    FGameplayTag SelectedBuildingTag;
    FGameplayTag SelectedMaterialTag;

    int32 RotationIndex = 0;

    FIntPoint CurrentHoveredCell = FIntPoint::ZeroValue;

    // 3D hover target. Tracked alongside CurrentHoveredCell so existing
    // 2D UI bindings keep working unchanged. Populated by UpdateFromMouse
    // from either terrain-derived Z or support-block face attachment.
    FIntVector CurrentHoveredVoxel = FIntVector::ZeroValue;

    // True voxel-space coordinate used only by TileGridOverlayActor.
    // Do not use this for farm zone / settlement-local logic.
    FIntVector CurrentOverlayVoxel = FIntVector::ZeroValue;

    bool bHoveredCellValid = false;
    float LastHitZ = 0.0f;

    // World-space terrain Z at the current hovered voxel.
    // Fed directly to GridOverlay so it can Z-sample tile placement
    // without redundant line traces.
    float CurrentHoveredTerrainZ = 0.0f;

    // CUBE-MINING-01 MVP: absolute voxel Z used for surface mine designations.
    int32 ActiveMiningZ = INDEX_NONE;

    // Suppresses repeated confirm input immediately after a successful commit.
    bool bJustCommitted = false;

    // Keeps last valid hover so the ghost stays stable when the cursor
    // briefly loses a clean trace hit while moving quickly.
    bool bHasLastValidVoxel = false;
    FIntVector LastValidVoxel = FIntVector::ZeroValue;

    // Click-and-drag continuous build support.
    bool bIsDraggingBuild = false;
    FIntVector LastPlacedDragVoxel = FIntVector(INT32_MIN);

    // Proper drag anchor state owned by the subsystem.
    bool bHasDragStart = false;
    FIntVector DragStartVoxel = FIntVector::ZeroValue;
    bool bDragStartedFromBlock = false;

    // Generic build/custom draw selection.
    TArray<FIntPoint> SelectedCells;
    TArray<FIntVector> SelectedCells3D;

    // Farm mode persistent paint cache.
    //
    // Why this exists:
    // Farm mode is currently layered on top of CustomDraw selection behavior,
    // and old/legacy build paths or widget refreshes can clear SelectedCells /
    // SelectedCells3D before Confirm runs. This cache preserves the painted
    // world cells specifically for CommitFarmZone().
    TSet<FIntPoint> FarmPaintedWorldCells;

    // UI-selected crop row for the next farm zone. Does not consume seeds.
    FName SelectedFarmCropRow = NAME_None;

    int32 ActiveSettlementID = -1;
    int32 BuildAgentID = -1;

    UPROPERTY()
    TObjectPtr<ABuildGhostActor> GhostActor = nullptr;

    bool bDataTablePreviewActive = false;
    FName DataTablePreviewSourceID = NAME_None;
    bool bDataTablePreviewIsPrefab = false;

    UPROPERTY()
    TObjectPtr<UStaticMesh> DataTablePreviewMesh = nullptr;

    // ── Grid Overlay (replaces TileSelectionOverlayActor) ──

    UPROPERTY()
    TObjectPtr<ATileGridOverlayActor> GridOverlay = nullptr;

    // Per-mode icon textures. Assign in a Blueprint subclass of this
    // subsystem or via InitializeGridOverlay() with LoadObject paths.
    // 128×128 or 256×256 white-on-transparent PNG recommended.

    UPROPERTY(EditDefaultsOnly, Category = "Build|Overlay")
    TObjectPtr<UTexture2D> BuildIconTexture;      // hammer

    UPROPERTY(EditDefaultsOnly, Category = "Build|Overlay")
    TObjectPtr<UTexture2D> FarmIconTexture;       // wheat

    UPROPERTY(EditDefaultsOnly, Category = "Build|Overlay")
    TObjectPtr<UTexture2D> MineIconTexture;       // pickaxe

    UPROPERTY(EditDefaultsOnly, Category = "Build|Overlay")
    TObjectPtr<UTexture2D> PrefabIconTexture;     // wrench (optional, nullptr OK)

    // ----- Legacy / Debug Dig Ghost -----

    UPROPERTY(EditDefaultsOnly, Category = "Build|Dig")
    TSubclassOf<ADigGhostActor> DigGhostClass;

    UPROPERTY()
    TObjectPtr<ADigGhostActor> ActiveDigGhost = nullptr;

    UPROPERTY()
    TObjectPtr<UDigSubsystem> CachedDigSubsystem = nullptr;

    // ----- Mine Mode Ghost Cube -----
    // 3D voxel-sized hover preview cube shown at the hovered voxel in Mine mode.
    // Separate from the 2D tile overlay (TileGridOverlayActor) which shows confirmed
    // designation cells. The ghost cube provides volumetric hover feedback before
    // the player clicks. Assign BP_MiningGhostActor in Class Defaults.

    UPROPERTY(EditDefaultsOnly, Category = "Build|Mine")
    TSubclassOf<AMiningGhostActor> MiningGhostClass;

    UPROPERTY()
    TObjectPtr<AMiningGhostActor> ActiveMiningGhost = nullptr;

    // Dedicated renderer for mine-mode terrain layer blocks and ghost designations.
    UPROPERTY()
    TObjectPtr<AMiningLayerRenderer> MiningLayerRenderer = nullptr;

    // Stable anchor for the underground mining layer renderer.
    // Do not center the cap renderer directly on the mouse every tick; that makes
    // the whole layer window pop/flicker as the cursor crosses voxel cells.
    bool bMiningLayerAnchorValid = false;
    FIntVector MiningLayerAnchorVoxel = FIntVector::ZeroValue;
    int32 MiningLayerAnchorZ = MIN_int32;
    FVector2D MiningLayerAnchorWorldXY = FVector2D::ZeroVector;

    bool bLastMiningVisualProofLogValid = false;
    int32 LastMiningVisualProofActiveZ = MIN_int32;
    int32 LastMiningVisualProofRelative = MIN_int32;
    int32 LastMiningVisualProofCapTerrain = MIN_int32;
    int32 LastMiningVisualProofSelectedGhost = MIN_int32;
    int32 LastMiningVisualProofConfirmedGhost = MIN_int32;
    int32 LastMiningVisualProofOverlaySelected = MIN_int32;
    int32 LastMiningVisualProofVolumeGhost = MIN_int32;
    bool bLastMiningVisualProofCutaway = false;
    bool bLastMiningVisualProofRenderer = false;

    TSet<TWeakObjectPtr<AActor>> MiningPresentationHiddenActors;

    // ================================
    // Subsystem helpers
    // ================================

    UPlayerBuildContext* GetBuildContext() const;
    UVoxelWorldManager* GetVoxelManager() const;
    UVoxelConstructionSystem* GetConstructionSystem() const;
    USettlementManager* GetSettlementManager() const;
    UDigSubsystem* GetDigSubsystem();
    UMiningSubsystem* GetMiningSubsystem() const;
    UFarmingSubsystem* GetFarmingSubsystem() const;
    UGridSelectionSubsystem* GetGridSelectionSubsystem() const;
    ULayerViewSubsystem* GetLayerViewSubsystem() const;

    int32 GetEffectiveSettlementID() const;

    // ================================
    // Commit helpers
    // ================================

    bool CommitPrefabBuild();
    bool CommitCustomBuild();
    bool CommitFarmZone();
    bool SelectNextAvailableFarmSeed(int32 SettlementID);
    bool CanPlaceDataTableBuildPiece(FString& OutFailureReason) const;
    bool CommitDataTableBuildPiecePlacement();

    // ================================
    // Visual helpers
    // ================================

    void SpawnGhost();
    void DestroyGhost();
    void UpdateGhost();

    // ── Grid Overlay lifecycle ──
    // These replace the old SpawnTileSelectionOverlay / DestroyTileSelectionOverlay /
    // UpdateTileSelectionOverlay family. The new overlay is a single actor that
    // persists across mode changes — mode switch just recolors + swaps icons.

    /** Spawn the GridOverlay actor and register all per-mode styles.
     *  Called once from Initialize(). Safe to call again (no-ops if alive). */
    void InitializeGridOverlay();

    /** Destroy the GridOverlay actor. Called from Deinitialize(). */
    void DestroyGridOverlay();

    /** Push current hover / selection state to the GridOverlay.
     *  Called every frame from UpdateFromMouse() when a mode is active. */
    void UpdateGridOverlay();

    /** Activate the overlay for the given tool mode (sets mode, shows). */
    void ActivateGridOverlayForMode(EBuildToolMode ToolMode);

    /** Hide overlay and clear all visual state. */
    void DeactivateGridOverlay();

    // Dedicated mine-layer renderer lifecycle.
    void InitializeMiningLayerRenderer();
    void DestroyMiningLayerRenderer(const TCHAR* Reason = TEXT("Destroy"));
    void UpdateMiningLayerRenderer();

    /**
     * Called each tick while in mine mode. Constrains the camera pawn's XY
     * position to stay within the diorama walls so the player can't pan the
     * camera outside the underground box and see the grass terrain beyond.
     */
    void ClampCameraToMineRegion();

    // Mine-mode presentation filter: hides above-ground/outside-diorama colonists
    // so the underground view reads as a focused slice instead of an open world.
    void UpdateMiningPresentationVisibility(const FIntVector& CenterVoxel, int32 ActiveVoxelZ, bool bUnderground);
    void RestoreMiningPresentationVisibility();

    void SyncSelectionFromGridSelection();
    void ClearTileSelectionState();
    void AddOrToggleCurrentTileSelection(bool bToggleExisting);
    bool CommitMineDesignations();

    void BroadcastSelectionChanged();
    void BroadcastFeedback(const FString& Message);

    // ================================
    // Coordinate helpers
    // ================================

    /** Convert a world-space position to a voxel coordinate (floor-divide by VoxelSize). */
    static FIntVector WorldToVoxel(const FVector& WorldPos, float VoxelSize = 100.0f);

    /** Convert voxel coordinates to terrain cell (for FarmingSubsystem compat). */
    static FIntPoint VoxelToTerrainCell(const FIntVector& VoxelCoord, int32 VoxelsPerCell = 32);

    // ================================
    // Trace helpers
    // ================================

    bool RaycastToTerrain(
        APlayerController* PC,
        FVector& OutHitLocation,
        bool& bOutHitPlacedBlock,
        FIntVector& OutSupportBlockVoxel,
        FVector& OutHitNormal) const;

    // ----- Legacy / Debug Dig Mode Tick -----

    void TickDigMode(float DeltaTime);
    bool RaycastDigTarget(FIntVector& OutGridPos, FVector& OutWorldPos) const;

    // Terrain validation helpers.
    float GetMaxGroundBuildSlopeDegrees() const;
    bool IsBuildableTerrainType(EWorldForgeTerrainType TerrainType) const;
    bool ValidateGroundTerrainPlacement(const FIntVector& Voxel, FString& OutReason) const;

    // Shared claimed-land gate. Build/mine/dig/farm wrappers all route here.
    bool IsVoxelCellInsidePlayerClaim(const FIntPoint& VoxelCell, const TCHAR* ToolLabel) const;

    bool IsBuildVoxelInsidePlayerClaim(const FIntVector& Voxel) const;
    bool IsDigVoxelInsidePlayerClaim(const FIntVector& Voxel) const;
    bool IsMineVoxelInsidePlayerClaim(const FIntVector& Voxel) const;
    bool IsFarmCellInsidePlayerClaim(const FIntPoint& WorldCell) const;
};

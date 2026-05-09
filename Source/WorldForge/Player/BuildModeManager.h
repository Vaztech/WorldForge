#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BuildModeTypes.h"
#include "VoxelWorldManager.h"
#include "WorldForgeDataTypes.h"
#include "Player/DigResult.h"
#include "BuildModeManager.generated.h"

class ABuildGhostActor;
class ADigGhostActor;
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
class UTexture2D;

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

    int32 ActiveSettlementID = -1;
    int32 BuildAgentID = -1;

    UPROPERTY()
    TObjectPtr<ABuildGhostActor> GhostActor = nullptr;

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

    int32 GetEffectiveSettlementID() const;

    // ================================
    // Commit helpers
    // ================================

    bool CommitPrefabBuild();
    bool CommitCustomBuild();
    bool CommitFarmZone();

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
};
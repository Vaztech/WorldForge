// WorldForgePlayerController.h
//
// ═══════════════════════════════════════════════════════════════════════════════
// INPUT ARCHITECTURE RULE — READ BEFORE ADDING ANY INPUT
// ═══════════════════════════════════════════════════════════════════════════════
//
// ALL player input in WorldForge must be routed through Enhanced Input Actions.
//
// NEVER use InputComponent->BindKey, BindAxis, or any legacy UE4 input path.
// NEVER hardcode EKeys::* values anywhere in C++ input setup.
//
// The correct pattern for every new input is:
//
//   1. Create a UInputAction asset in /Game/WorldForge/Controls/.
//   2. Add it to IMC_BuildMode (or the appropriate IMC) with a default key mapping.
//   3. Declare it here as:
//        UPROPERTY(EditAnywhere) TObjectPtr<UInputAction> IA_YourAction;
//   4. Add a ConstructorHelpers::FObjectFinder in the constructor as a native
//      fallback so the binding survives Blueprint refresh / CDO reset.
//   5. Bind it in SetupInputComponent via EIC->BindAction(IA_YourAction, ...).
//
// For actions that should also be triggerable from UI buttons:
//   - Implement the logic in a public BlueprintCallable Execute* method.
//   - The Enhanced Input handler calls that Execute* method.
//   - The UI button also calls that Execute* method directly.
//   - One code path. No duplication.
//
// WHY THIS MATTERS:
//   Enhanced Input actions are exposed to the runtime remapping system, which
//   means the controls settings UI can let the player rebind any key without
//   touching C++. Hardcoded BindKey calls cannot be remapped and are invisible
//   to the controls settings UI.
//
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgePlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UBuildModeManager;
class UMineLayerCoordinator;
class AWorldForgeCamera;
class USimulationTimeManager;
class AWorldForgeMiniMapHUD;
class AWorldForgeColonist;
class UProcessorManager;
class AWorldGenBootstrapActor;

UCLASS()
class WORLDFORGE_API AWorldForgePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AWorldForgePlayerController();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool IsBuildCameraSuppressed() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
    bool ShouldLockBuildCamera() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|Camera")
    AWorldForgeCamera* GetWorldForgeCamera() const { return WorldForgeCamera; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Selection")
    bool SelectColonistByAgentID(int32 AgentID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Selection")
    bool SelectColonistActor(AWorldForgeColonist* ColonistActor);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Selection")
    bool SelectColonistUnderCursor();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Selection")
    void ClearSelectedColonist();

    UFUNCTION(BlueprintPure, Category = "WorldForge|Colonist|Selection")
    int32 GetSelectedAgentID() const { return SelectedAgentID; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Inspection")
    bool GetSelectedColonistInspectionData(FColonistInspectionData& OutData) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colonist|Jobs")
    bool SetSelectedColonistJobPriority(FGameplayTag JobTag, int32 Priority);

    // =========================================================================
    // Public action executors
    //
    // These are the single source of truth for each player action.
    // Enhanced Input handlers call these. UI buttons call these.
    // Same code path regardless of how the action was triggered.
    // =========================================================================

    /** Adjust the tool layer (which voxel Z layer edits target). Delta: +1 up, -1 down. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void ExecuteToolLayerAdjust(int32 Delta);

    /** Reset the tool layer to the surface (offset 0). */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Layer")
    void ExecuteToolLayerReset();

    /**
     * Adjust the view layer (which underground layer is visible via terrain clip plane).
     * Delta: -1 goes one layer deeper, +1 comes one layer back toward surface.
     * Only has effect while TerrainCutawaySubsystem is active (Mine mode).
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway|Layer")
    void ExecuteViewLayerAdjust(int32 Delta);

    /** Reset the view layer to surface without exiting Mine mode. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Cutaway|Layer")
    void ExecuteViewLayerReset();

    /** Toggle simulation pause. Call from UI pause button or Space key. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Global")
    void ExecutePauseToggle();

    /** Toggle minimap visibility. Call from UI button or M key. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Global")
    void ExecuteMiniMapToggle();

    /** Toggle/enter mine mode. Call from UI Mine button or input action. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    void ExecuteToggleMineMode();

    /**
     * Force-exit mine mode and its controller-owned cutaway/camera/layer state.
     * Safe to call before switching to any non-mine build tool.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Build|Mining")
    void ExecuteExitMineModeIfActive();

    // =========================================================================
    // Simulation speed controls
    //
    // Four speed levels: 1x, 2x, 3x, 4x.
    // Target time scale: 1 in-game hour = 1 real minute at 1x speed.
    //   Full day  = 120,000 ticks  (60,000 day / 60,000 night)
    //   1x  → 24 real minutes per game day
    //   2x  → 12 real minutes per game day
    //   3x  →  8 real minutes per game day
    //   4x  →  6 real minutes per game day
    //
    // Keys (set in IMC_BuildMode, not hardcoded here):
    //   +  (NumPad Add)      → speed up one level
    //   -  (NumPad Subtract) → slow down one level
    //   *  (NumPad Multiply) → reset to 1x
    // =========================================================================

    /**
     * Step simulation speed up one level (1x → 2x → 3x → 4x).
     * Clamped at maximum. Call from UI speed-up button or + key.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|SimSpeed")
    void ExecuteSimSpeedUp();

    /**
     * Step simulation speed down one level (4x → 3x → 2x → 1x).
     * Clamped at minimum. Call from UI slow-down button or - key.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|SimSpeed")
    void ExecuteSimSpeedDown();

    /**
     * Reset simulation speed to 1x regardless of current level.
     * Call from UI reset button or * key.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|SimSpeed")
    void ExecuteSimSpeedReset();

    /** Returns the current speed multiplier (1.0, 2.0, 3.0, or 4.0). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|SimSpeed")
    float GetCurrentSimSpeedMultiplier() const;

    /** Returns the current speed level index (0=1x, 1=2x, 2=3x, 3=4x). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|SimSpeed")
    int32 GetCurrentSimSpeedIndex() const { return CurrentSimSpeedIndex; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Camera")
    void NotifyCameraHandoffComplete();

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;

    // =========================================================================
    // Input Mapping Context
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    TSoftObjectPtr<UInputMappingContext> GlobalMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    int32 GlobalMappingPriority = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    TSoftObjectPtr<UInputMappingContext> BuildMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    int32 BuildMappingPriority = 50;

    // =========================================================================
    // Build mode actions
    // Default keys assigned in IMC_BuildMode — NOT hardcoded here.
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildClick;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildSecondary;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildRotate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildConfirm;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildCancel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildToggle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build")
    TObjectPtr<UInputAction> IA_BuildUndo;

    // =========================================================================
    // Tool layer actions
    // Control which voxel Z layer the active tool (build/mine/farm) targets.
    // Default keys: ] up, [ down, 0 reset — set in IMC_BuildMode.
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ToolLayer")
    TObjectPtr<UInputAction> IA_ToolLayerUp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ToolLayer")
    TObjectPtr<UInputAction> IA_ToolLayerDown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ToolLayer")
    TObjectPtr<UInputAction> IA_ToolLayerReset;

    // =========================================================================
    // View layer actions
    // Control which underground layer is revealed by the terrain clip plane.
    // Only meaningful while in Mine mode (TerrainCutawaySubsystem active).
    // Default keys: . down, , up, / reset — set in IMC_BuildMode.
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ViewLayer")
    TObjectPtr<UInputAction> IA_ViewLayerDown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ViewLayer")
    TObjectPtr<UInputAction> IA_ViewLayerUp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|ViewLayer")
    TObjectPtr<UInputAction> IA_ViewLayerReset;

    // =========================================================================
    // Global actions
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Global")
    TObjectPtr<UInputAction> IA_TogglePause;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Global")
    TObjectPtr<UInputAction> IA_ToggleMiniMap;

    // =========================================================================
    // Simulation speed actions
    // Default keys: NumPad+ up, NumPad- down, NumPad* reset — set in IMC_BuildMode.
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|SimSpeed")
    TObjectPtr<UInputAction> IA_SimSpeedUp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|SimSpeed")
    TObjectPtr<UInputAction> IA_SimSpeedDown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|SimSpeed")
    TObjectPtr<UInputAction> IA_SimSpeedReset;

    // =========================================================================
    // Debug / tool toggle actions
    // =========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Debug")
    TObjectPtr<UInputAction> IA_ToggleDigMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Debug")
    TObjectPtr<UInputAction> IA_ToggleMineMode;

    // =========================================================================
    // UI
    // =========================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|UI")
    TSubclassOf<UUserWidget> BuildWidgetClass;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|UI")
    TObjectPtr<UUserWidget> BuildWidgetInstance;

    // =========================================================================
    // Cached runtime refs
    // =========================================================================

    UPROPERTY(Transient, BlueprintReadOnly, Category = "WorldForge|Colonist|Selection", meta = (AllowPrivateAccess = "true"))
    int32 SelectedAgentID = INDEX_NONE;

    UPROPERTY(Transient)
    TObjectPtr<UBuildModeManager> BuildModeManager;

    /** Coordinator that atomically syncs LayerView + Cutaway + Grid for mine-mode layer changes. */
    UPROPERTY(Transient)
    TObjectPtr<UMineLayerCoordinator> MineLayerCoord;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build",
        meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float BuildClickDebounceSeconds = 0.2f;

    UFUNCTION()
    void ToggleDigMode();

    UFUNCTION()
    void ToggleMineMode();

private:
    // =========================================================================
    // Enhanced Input handlers (private)
    //
    // These exist solely to forward input events to the public Execute* methods.
    // They are bound to IA_ actions in SetupInputComponent and must not contain
    // gameplay logic themselves. UI code should never call these directly —
    // call the corresponding Execute* method instead.
    // =========================================================================

    void OnBuildClickStarted(const FInputActionValue& Value);
    void OnBuildClickReleased(const FInputActionValue& Value);
    void OnBuildSecondary(const FInputActionValue& Value);
    void OnBuildRotate(const FInputActionValue& Value);
    void OnBuildConfirm(const FInputActionValue& Value);
    void OnBuildCancel(const FInputActionValue& Value);
    void OnBuildToggle(const FInputActionValue& Value);
    void OnBuildUndo(const FInputActionValue& Value);

    void OnToolLayerUp();
    void OnToolLayerDown();
    void OnToolLayerReset();

    void OnViewLayerDown();
    void OnViewLayerUp();
    void OnViewLayerReset();

    void OnPauseToggle(const FInputActionValue& Value);
    void OnMiniMapToggle(const FInputActionValue& Value);

    void OnSimSpeedUp();
    void OnSimSpeedDown();
    void OnSimSpeedReset();

    // =========================================================================
    // State helpers
    // =========================================================================

    bool IsAnyBuildModeActive() const;
    bool IsCustomDrawActive() const;
    void RefreshBuildInputSuppression();

    USimulationTimeManager* GetSimulationTimeManager() const;
    AWorldForgeMiniMapHUD*  GetMiniMapHUD() const;

    void SetBuildWidgetVisible(bool bVisible);
    void ToggleMiniMapVisibility();

    bool AcquireAndApplyCameraViewTarget(bool bLogResult);

    void EnterMiningSubsystems();
    void ExitMiningSubsystems();

    /** Apply the speed at CurrentSimSpeedIndex to SimulationTimeManager. */
    void ApplyCurrentSimSpeed();

    // =========================================================================
    // State
    // =========================================================================

    UPROPERTY(Transient)
    TObjectPtr<AWorldForgeCamera> WorldForgeCamera = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<AWorldGenBootstrapActor> CachedBootstrapActor = nullptr;

    bool   bBuildWidgetVisible           = false;
    bool   bBuildInputSuppressed         = false;
    bool   bCameraViewTargetApplied      = false;
    bool   bPrePlayBlockedByBootstrap    = false;
    bool   bHasCheckedPrePlayBlocking    = false;

    double LastBuildClickTime       = 0.0;
    double LastPauseToggleTime      = -1000.0;
    double LastMiniMapToggleTime    = -1000.0;

    /**
     * Current index into the SimSpeedLevels array (defined in .cpp).
     * 0 = 1x, 1 = 2x, 2 = 3x, 3 = 4x.
     */
    int32 CurrentSimSpeedIndex = 0;

    static constexpr double ToggleDebounceSeconds = 0.12;
};

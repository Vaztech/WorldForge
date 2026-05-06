// WorldForgePlayerController.h
// Replaces BP_WorldForgePlayerController entirely.
// Owns IMC_BuildMode at priority 1. Routes build/global input to C++.
// Camera ownership/view target is also enforced here in C++.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "WorldForgePlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UBuildModeManager;
class UBuildModeWidget;
class AWorldForgeCamera;
class USimulationTimeManager;
class AWorldForgeMiniMapHUD;

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
    AWorldForgeCamera* GetWorldForgeCamera() const
    {
        return WorldForgeCamera;
    }

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;

    // ================================
    // Input Mapping
    // ================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    TSoftObjectPtr<UInputMappingContext> BuildMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge")
    int32 BuildMappingPriority = 1;

    // ================================
    // Build Input
    // ================================

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

    // ================================
    // Global Input
    // ================================
    // Space = pause/unpause simulation.
    // M     = show/hide HUD minimap.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Global")
    TObjectPtr<UInputAction> IA_TogglePause;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Global")
    TObjectPtr<UInputAction> IA_ToggleMiniMap;

    // ================================
    // Debug / Legacy Tool Input
    // ================================
    // Dig is no longer intended as a player-facing separate mode.
    // The future player-facing tool should be unified under Mine/Excavate.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Debug")
    TObjectPtr<UInputAction> IA_ToggleDigMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Debug")
    TObjectPtr<UInputAction> IA_ToggleMineMode;

    // ================================
    // UI
    // ================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|WorldForge")
    TSubclassOf<UBuildModeWidget> BuildWidgetClass;

    // ================================
    // Cached runtime refs
    // ================================

    UPROPERTY(Transient)
    TObjectPtr<UBuildModeManager> BuildModeManager;

    UPROPERTY(Transient)
    TObjectPtr<UBuildModeWidget> BuildWidgetInstance;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|WorldForge|Build", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float BuildClickDebounceSeconds = 0.2f;

    UFUNCTION()
    void ToggleDigMode();

    UFUNCTION()
    void ToggleMineMode();

private:
    // ================================
    // Input handlers
    // ================================

    void OnBuildClickStarted(const FInputActionValue& Value);
    void OnBuildClickReleased(const FInputActionValue& Value);
    void OnBuildSecondary(const FInputActionValue& Value);
    void OnBuildRotate(const FInputActionValue& Value);
    void OnBuildConfirm(const FInputActionValue& Value);
    void OnBuildCancel(const FInputActionValue& Value);
    void OnBuildToggle(const FInputActionValue& Value);
    void OnBuildUndo(const FInputActionValue& Value);

    void OnPauseToggle(const FInputActionValue& Value);
    void OnMiniMapToggle(const FInputActionValue& Value);

    // Legacy fallback key handlers.
    // These help while Enhanced Input mappings are being cleaned up.
    void OnPauseKeyPressed();
    void OnMiniMapKeyPressed();

    // Shared handlers used by both Enhanced Input and fallback key input.
    void ExecutePauseToggle();
    void ExecuteMiniMapToggle();

    // ================================
    // State helpers
    // ================================

    bool IsAnyBuildModeActive() const;
    bool IsCustomDrawActive() const;
    void RefreshBuildInputSuppression();

    USimulationTimeManager* GetSimulationTimeManager() const;
    AWorldForgeMiniMapHUD* GetMiniMapHUD() const;

    void SetBuildWidgetVisible(bool bVisible);
    void ToggleMiniMapVisibility();

    bool AcquireAndApplyCameraViewTarget(bool bLogResult);

private:
    UPROPERTY(Transient)
    TObjectPtr<AWorldForgeCamera> WorldForgeCamera = nullptr;

    bool bBuildWidgetVisible = false;
    bool bBuildInputSuppressed = false;
    bool bCameraViewTargetApplied = false;

    double LastBuildClickTime = 0.0;

    // Prevents duplicate Enhanced Input + fallback key events from toggling twice.
    double LastPauseToggleTime = -1000.0;
    double LastMiniMapToggleTime = -1000.0;

    static constexpr double ToggleDebounceSeconds = 0.12;
};
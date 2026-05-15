// Source/WorldForge/Player/WorldForgeCamera.h
//
// Changes vs previous version:
//   - Added UMiningCameraMode* ActiveMiningCameraMode (UPROPERTY, created in BeginPlay).
//   - Added EnterMiningCameraMode / ExitMiningCameraMode public methods.
//   - Added HandleViewLayerChanged UFUNCTION (bound to TerrainCutawaySubsystem::OnViewLayerChanged).
//   - All other members are unchanged.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "WorldForgeCamera.generated.h"

class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UMiningCameraMode;   // forward-declared; included in .cpp only

UCLASS(Blueprintable)
class WORLDFORGE_API AWorldForgeCamera : public APawn
{
    GENERATED_BODY()

public:
    AWorldForgeCamera();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

    // ─── Existing snap helper ────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Camera")
    void SnapToRTSView(const FVector& NewLocation, float NewYaw, float NewPitch,
        float NewZoom, bool bApplyImmediately = true);

    // ─── Mining camera mode ──────────────────────────────────────────────────

    /**
     * Enter mining camera mode.
     * Saves current pitch/zoom constraints, applies looser mining constraints,
     * and marks this camera as tracking underground view layers.
     *
     * @param SurfaceWorldZ  World-space Z of the terrain surface at the camera pivot.
     *                       Pass the camera's current actor location Z as an approximation.
     */
    UFUNCTION(BlueprintCallable, Category = "Camera|Mining")
    void EnterMiningCameraMode(float SurfaceWorldZ);

    /**
     * Exit mining camera mode.
     * Restores original pitch/zoom constraints and resets TargetLocation.Z to surface.
     * Safe to call when not in mining mode (no-op).
     */
    UFUNCTION(BlueprintCallable, Category = "Camera|Mining")
    void ExitMiningCameraMode();

    UFUNCTION(BlueprintPure, Category = "Camera|Mining")
    bool IsInMiningCameraMode() const;

    /**
     * Clamp and optionally snap the RTS camera into the active mining view limits.
     * Used when entering mine mode so a far zoomed-out surface camera cannot see
     * outside the underground diorama presentation.
     */
    UFUNCTION(BlueprintCallable, Category = "Camera|Mining")
    void ApplyMiningCameraView(float DesiredPitch, float DesiredZoom, bool bApplyImmediately = false);

    /**
     * Responds to TerrainCutawaySubsystem::OnViewLayerChanged.
     * Shifts TargetLocation.Z so the camera pivot follows the active underground layer.
     * Bound via AddDynamic in BeginPlay.
     */
    UFUNCTION()
    void HandleViewLayerChanged(int32 NewLayerOffset);

    // ─── Components ──────────────────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    USceneComponent* DefaultSceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    USpringArmComponent* CameraArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    UCameraComponent* ColonyCamera;

    // ─── Input assets ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* IMC_WorldForgeCamera = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Move = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_MousePan = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Zoom = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_RotateKeys = nullptr;

    // ─── Movement ────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement",
        meta = (ClampMin = "100.0", ClampMax = "20000.0", UIMin = "500.0", UIMax = "8000.0"))
    float MoveSpeed = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement",
        meta = (ClampMin = "1.0", ClampMax = "1000.0", UIMin = "10.0", UIMax = "300.0"))
    float MousePanSpeed = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
    bool bInvertPanX = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
    bool bInvertPanY = true;

    // ─── Zoom ────────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom",
        meta = (ClampMin = "10.0", ClampMax = "3000.0", UIMin = "50.0", UIMax = "1000.0"))
    float ZoomStep = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom",
        meta = (ClampMin = "10.0", ClampMax = "5000.0", UIMin = "25.0", UIMax = "1000.0"))
    float ZoomMin = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom",
        meta = (ClampMin = "500.0", ClampMax = "50000.0", UIMin = "3000.0", UIMax = "20000.0"))
    float ZoomMax = 20000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom",
        meta = (ClampMin = "50.0", ClampMax = "10000.0", UIMin = "300.0", UIMax = "3000.0"))
    float DefaultArmLength = 1500.0f;

    // ─── Rotation ────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation",
        meta = (ClampMin = "10.0", ClampMax = "1000.0", UIMin = "60.0", UIMax = "500.0"))
    float RotationSpeedKeys = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation",
        meta = (ClampMin = "0.01", ClampMax = "10.0", UIMin = "0.05", UIMax = "2.0"))
    float RotationSpeedMouse = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation",
        meta = (ClampMin = "-89.0", ClampMax = "0.0", UIMin = "-89.0", UIMax = "-30.0"))
    float MinPitch = -85.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation",
        meta = (ClampMin = "-89.0", ClampMax = "0.0", UIMin = "-45.0", UIMax = "-5.0"))
    float MaxPitch = -15.0f;

    // ─── Smoothing ───────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Smoothing",
        meta = (ClampMin = "0.1", ClampMax = "50.0", UIMin = "1.0", UIMax = "20.0"))
    float LocationLagSpeed = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Smoothing",
        meta = (ClampMin = "0.1", ClampMax = "50.0", UIMin = "1.0", UIMax = "20.0"))
    float ZoomLagSpeed = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Smoothing",
        meta = (ClampMin = "0.1", ClampMax = "50.0", UIMin = "1.0", UIMax = "20.0"))
    float RotationLagSpeed = 10.0f;

private:
    // ─── Input handlers ──────────────────────────────────────────────────────

    void HandleMove(const FInputActionValue& Value);
    void HandleMousePan(const FInputActionValue& Value);
    void HandleZoom(const FInputActionValue& Value);
    void HandleRotateKeys(const FInputActionValue& Value);

    float GetRotateKeyAxisFromKeyboard() const;

    // ─── Camera state ────────────────────────────────────────────────────────

    FVector TargetLocation = FVector::ZeroVector;
    float   TargetZoom     = 0.0f;
    float   TargetYaw      = 0.0f;
    float   TargetPitch    = -50.0f;

    // ─── Mining camera mode ──────────────────────────────────────────────────

    /** Manages pitch/zoom overrides and layer-Z tracking while in mine mode.
     *  Created in BeginPlay; UPROPERTY prevents GC. */
    UPROPERTY()
    TObjectPtr<UMiningCameraMode> ActiveMiningCameraMode = nullptr;
};

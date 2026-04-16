// WorldForgePlayerController.h
// Replaces BP_WorldForgePlayerController entirely.
// Owns IMC_BuildMode at priority 1. Routes all build input to BuildModeManager.
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

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TSoftObjectPtr<UInputMappingContext> BuildMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    int32 BuildMappingPriority = 1;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildClick;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildSecondary;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildRotate;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildConfirm;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildCancel;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildToggle;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
    TObjectPtr<UInputAction> IA_BuildUndo;

    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UBuildModeWidget> BuildWidgetClass;

    UPROPERTY(Transient)
    TObjectPtr<UBuildModeManager> BuildModeManager;

    UPROPERTY(Transient)
    TObjectPtr<UBuildModeWidget> BuildWidgetInstance;

    UPROPERTY(EditDefaultsOnly, Category = "Input|Build", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float BuildClickDebounceSeconds = 0.2f;

private:
    void OnBuildClickStarted(const FInputActionValue& Value);
    void OnBuildClickReleased(const FInputActionValue& Value);
    void OnBuildSecondary(const FInputActionValue& Value);
    void OnBuildRotate(const FInputActionValue& Value);
    void OnBuildConfirm(const FInputActionValue& Value);
    void OnBuildCancel(const FInputActionValue& Value);
    void OnBuildToggle(const FInputActionValue& Value);
    void OnBuildUndo(const FInputActionValue& Value);

    bool IsAnyBuildModeActive() const;
    bool IsCustomDrawActive() const;
    void RefreshBuildInputSuppression();

    bool AcquireAndApplyCameraViewTarget(bool bLogResult);

private:
    UPROPERTY(Transient)
    TObjectPtr<AWorldForgeCamera> WorldForgeCamera = nullptr;

    bool bBuildWidgetVisible = false;
    bool bBuildInputSuppressed = false;
    bool bCameraViewTargetApplied = false;

    double LastBuildClickTime = 0.0;
};
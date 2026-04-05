// WorldForgePlayerController.h
// Replaces BP_WorldForgePlayerController entirely.
// Owns IMC_BuildMode at priority 1. Routes all build input to BuildModeManager.
// Camera (BP_WorldForgeCamera) stays untouched — it owns IMC_WorldForgeCamera at priority 0.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "WorldForgePlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class UBuildModeManager;
class UBuildModeWidget;

UCLASS()
class WORLDFORGE_API AWorldForgePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AWorldForgePlayerController();

	/** Exposed for BP_WorldForgeCamera so it can suppress its own pan/edge-scroll while building. */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	bool IsBuildCameraSuppressed() const;

	/**
	 * Returns true when WASD translation and Tick rotation should be locked.
	 * RMB pan is intentionally NOT locked — the camera Blueprint should gate
	 * IA_Move and Tick rotation on this, but let IA_MousePan pass through.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Build")
	bool ShouldLockBuildCamera() const;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
	TSoftObjectPtr<UInputMappingContext> BuildMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
	int32 BuildMappingPriority = 1;

	// --- Input Actions ---

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

	/** Z key — undo last cell in custom draw mode. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Build")
	TObjectPtr<UInputAction> IA_BuildUndo;

	// --- UI ---

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBuildModeWidget> BuildWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UBuildModeManager> BuildModeManager;

	UPROPERTY(Transient)
	TObjectPtr<UBuildModeWidget> BuildWidgetInstance;

	// --- Debounce ---

	/** Minimum seconds between accepted LMB build clicks. Prevents multi-fire. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Build", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BuildClickDebounceSeconds = 0.2f;

private:
	void OnBuildClick(const FInputActionValue& Value);
	void OnBuildSecondary(const FInputActionValue& Value);
	void OnBuildRotate(const FInputActionValue& Value);
	void OnBuildConfirm(const FInputActionValue& Value);
	void OnBuildCancel(const FInputActionValue& Value);
	void OnBuildToggle(const FInputActionValue& Value);
	void OnBuildUndo(const FInputActionValue& Value);

	bool IsAnyBuildModeActive() const;
	bool IsCustomDrawActive() const;
	void RefreshBuildInputSuppression();

	bool bBuildWidgetVisible = false;
	bool bBuildInputSuppressed = false;

	/** World time of the last accepted build click (for debounce). */
	double LastBuildClickTime = 0.0;
};

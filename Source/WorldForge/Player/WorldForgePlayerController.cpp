#include "WorldForgePlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Player/BuildModeManager.h"
#include "Player/BuildModeTypes.h"
#include "UI/BuildModeWidget.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Debug/WorldForgeCheatManager.h"
#include "WorldForgeCamera.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldForgePC, Log, All);

AWorldForgePlayerController::AWorldForgePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    CheatClass = UWorldForgeCheatManager::StaticClass();
}

void AWorldForgePlayerController::BeginPlay()
{
    Super::BeginPlay();

    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);

    if (UEnhancedInputLocalPlayerSubsystem* EIS = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        if (UInputMappingContext* IMC = BuildMappingContext.LoadSynchronous())
        {
            EIS->AddMappingContext(IMC, BuildMappingPriority);
            UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] IMC_BuildMode added at priority %d"), BuildMappingPriority);
        }
        else
        {
            UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] BuildMappingContext not set — build input will not work!"));
        }
    }

    BuildModeManager = GetWorld()->GetSubsystem<UBuildModeManager>();
    if (BuildModeManager)
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] BuildModeManager found and cached."));
    }
    else
    {
        UE_LOG(LogWorldForgePC, Error, TEXT("[WorldForgePC] BuildModeManager NOT FOUND — build mode will not function!"));
    }

    if (BuildWidgetClass)
    {
        BuildWidgetInstance = CreateWidget<UBuildModeWidget>(this, BuildWidgetClass);
        if (BuildWidgetInstance)
        {
            BuildWidgetInstance->AddToViewport(10);
            BuildWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
            bBuildWidgetVisible = false;
            UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] WBP_BuildMode created (hidden)."));
        }
    }

    AcquireAndApplyCameraViewTarget(true);
    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (IA_BuildClick)
        {
            EIC->BindAction(IA_BuildClick, ETriggerEvent::Started, this, &AWorldForgePlayerController::OnBuildClickStarted);
            EIC->BindAction(IA_BuildClick, ETriggerEvent::Completed, this, &AWorldForgePlayerController::OnBuildClickReleased);
            EIC->BindAction(IA_BuildClick, ETriggerEvent::Canceled, this, &AWorldForgePlayerController::OnBuildClickReleased);
        }

        if (IA_BuildSecondary)
        {
            EIC->BindAction(IA_BuildSecondary, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildSecondary);
        }

        if (IA_BuildRotate)
        {
            EIC->BindAction(IA_BuildRotate, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildRotate);
        }

        if (IA_BuildConfirm)
        {
            EIC->BindAction(IA_BuildConfirm, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildConfirm);
        }

        if (IA_BuildCancel)
        {
            EIC->BindAction(IA_BuildCancel, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildCancel);
        }

        if (IA_BuildToggle)
        {
            EIC->BindAction(IA_BuildToggle, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildToggle);
        }

        if (IA_BuildUndo)
        {
            EIC->BindAction(IA_BuildUndo, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildUndo);
        }

        // ===== NEW: DIG MODE =====
        if (IA_ToggleDigMode)
        {
            EIC->BindAction(IA_ToggleDigMode, ETriggerEvent::Started, this, &AWorldForgePlayerController::ToggleDigMode);
        }

        // ===== NEW: MINE MODE (stub) =====
        if (IA_ToggleMineMode)
        {
            EIC->BindAction(IA_ToggleMineMode, ETriggerEvent::Started, this, &AWorldForgePlayerController::ToggleMineMode);
        }

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Enhanced Input actions bound."));
    }
}

void AWorldForgePlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!bCameraViewTargetApplied)
    {
        AcquireAndApplyCameraViewTarget(false);
    }

    RefreshBuildInputSuppression();

    if (BuildModeManager && BuildModeManager->IsInBuildMode())
    {
        BuildModeManager->UpdateFromMouse(this);
    }
}

bool AWorldForgePlayerController::IsAnyBuildModeActive() const
{
    return BuildModeManager && BuildModeManager->IsInBuildMode();
}

bool AWorldForgePlayerController::IsCustomDrawActive() const
{
    return BuildModeManager && BuildModeManager->GetCurrentMode() == EBuildMode::CustomDraw;
}

bool AWorldForgePlayerController::ShouldLockBuildCamera() const
{
    return BuildModeManager && BuildModeManager->IsInBuildMode();
}

bool AWorldForgePlayerController::IsBuildCameraSuppressed() const
{
    return ShouldLockBuildCamera();
}

void AWorldForgePlayerController::RefreshBuildInputSuppression()
{
    const bool bShouldSuppress = false;

    if (bShouldSuppress == bBuildInputSuppressed)
    {
        return;
    }

    bBuildInputSuppressed = bShouldSuppress;
    SetIgnoreMoveInput(false);

    const bool bInBuildMode = BuildModeManager && BuildModeManager->IsInBuildMode();

    UE_LOG(
        LogWorldForgePC,
        Log,
        TEXT("[WorldForgePC] Camera input refreshed. InBuildMode=%s Suppressed=%s"),
        bInBuildMode ? TEXT("true") : TEXT("false"),
        bBuildInputSuppressed ? TEXT("true") : TEXT("false"));
}

bool AWorldForgePlayerController::AcquireAndApplyCameraViewTarget(bool bLogResult)
{
    APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn) return false;

    AWorldForgeCamera* CameraPawn = Cast<AWorldForgeCamera>(ControlledPawn);
    if (!CameraPawn) return false;

    WorldForgeCamera = CameraPawn;

    if (GetViewTarget() != CameraPawn)
    {
        SetViewTargetWithBlend(CameraPawn, 0.0f);
    }

    bCameraViewTargetApplied = true;

    if (bLogResult)
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] View target set to camera pawn."));
    }

    return true;
}

// ===================================================================
// INPUT
// ===================================================================

void AWorldForgePlayerController::OnBuildClickStarted(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;

    const double Now = GetWorld()->GetTimeSeconds();
    if ((Now - LastBuildClickTime) < BuildClickDebounceSeconds) return;

    LastBuildClickTime = Now;
    BuildModeManager->HandlePrimaryClick();
}

void AWorldForgePlayerController::OnBuildClickReleased(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->SetDragBuildActive(false);
}

void AWorldForgePlayerController::OnBuildSecondary(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleSecondaryClick();
}

void AWorldForgePlayerController::OnBuildRotate(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleRotate(true);
}

void AWorldForgePlayerController::OnBuildConfirm(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleConfirm();
}

void AWorldForgePlayerController::OnBuildCancel(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleCancel();
    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::OnBuildToggle(const FInputActionValue& Value)
{
    if (!BuildWidgetInstance) return;

    bBuildWidgetVisible = !bBuildWidgetVisible;

    if (bBuildWidgetVisible)
    {
        BuildWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    else
    {
        BuildWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);

        if (BuildModeManager && BuildModeManager->IsInBuildMode())
        {
            BuildModeManager->ExitBuildMode();
        }
    }

    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::OnBuildUndo(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleUndo();
}

// ===================================================================
// DIG MODE
// ===================================================================

void AWorldForgePlayerController::ToggleDigMode()
{
    if (!BuildModeManager) return;

    if (BuildModeManager->IsInDigMode())
    {
        BuildModeManager->ExitDigMode();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Dig Mode OFF"));
    }
    else
    {
        BuildModeManager->EnterDigMode();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Dig Mode ON"));
    }
}

// ===================================================================
// MINE MODE (STUB FOR PHASE 4)
// ===================================================================

void AWorldForgePlayerController::ToggleMineMode()
{
    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mine Mode toggle (not implemented yet)"));
}
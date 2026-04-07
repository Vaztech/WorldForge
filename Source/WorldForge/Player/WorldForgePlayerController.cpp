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

DEFINE_LOG_CATEGORY_STATIC(LogWorldForgePC, Log, All);

AWorldForgePlayerController::AWorldForgePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    // =========================================
    // DEBUG: Attach CheatManager
    // NOTE: Remove or disable before shipping
    // =========================================
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

    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (IA_BuildClick)
        {
            EIC->BindAction(IA_BuildClick, ETriggerEvent::Triggered, this, &AWorldForgePlayerController::OnBuildClick);
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

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Enhanced Input actions bound."));
    }
}

void AWorldForgePlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

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
    if (!BuildModeManager)
    {
        return false;
    }

    return BuildModeManager->IsInBuildMode();
}

bool AWorldForgePlayerController::IsBuildCameraSuppressed() const
{
    return ShouldLockBuildCamera();
}

void AWorldForgePlayerController::RefreshBuildInputSuppression()
{
    const bool bShouldSuppress = false; // WASD now allowed even in build mode.

    if (bShouldSuppress == bBuildInputSuppressed)
    {
        return;
    }

    bBuildInputSuppressed = bShouldSuppress;

    // Keep move input enabled.
    SetIgnoreMoveInput(false);

    const bool bInBuildMode = BuildModeManager && BuildModeManager->IsInBuildMode();

    UE_LOG(
        LogWorldForgePC,
        Log,
        TEXT("[WorldForgePC] Camera input state refreshed. InBuildMode=%s MoveSuppressed=%s"),
        bInBuildMode ? TEXT("true") : TEXT("false"),
        bBuildInputSuppressed ? TEXT("true") : TEXT("false"));
}

// ===================================================================
// Input handlers
// ===================================================================

void AWorldForgePlayerController::OnBuildClick(const FInputActionValue& Value)
{
    if (!BuildModeManager) return;
    if (!BuildModeManager->IsInBuildMode()) return;

    const UWorld* W = GetWorld();
    if (W)
    {
        const double Now = W->GetTimeSeconds();
        if ((Now - LastBuildClickTime) < BuildClickDebounceSeconds)
        {
            return;
        }
        LastBuildClickTime = Now;
    }

    BuildModeManager->HandlePrimaryClick();
}

void AWorldForgePlayerController::OnBuildSecondary(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;

    // Leave RMB to camera pan/orbit. BuildModeManager secondary is intentionally blank now.
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
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Build widget shown."));
    }
    else
    {
        BuildWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);

        if (BuildModeManager && BuildModeManager->IsInBuildMode())
        {
            BuildModeManager->ExitBuildMode();
        }

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Build widget hidden. Build mode exited."));
    }

    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::OnBuildUndo(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleUndo();
}
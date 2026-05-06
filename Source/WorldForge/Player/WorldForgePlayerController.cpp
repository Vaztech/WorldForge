#include "WorldForgePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"

#include "Player/BuildModeManager.h"
#include "Player/BuildModeTypes.h"
#include "UI/BuildModeWidget.h"

#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"

#include "Debug/WorldForgeCheatManager.h"
#include "WorldForgeCamera.h"
#include "GameFramework/Pawn.h"

#include "Colony/SimulationTimeManager.h"
#include "UI/WorldForgeMiniMapHUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldForgePC, Log, All);

AWorldForgePlayerController::AWorldForgePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    CheatClass = UWorldForgeCheatManager::StaticClass();

    // ---------------------------------------------------------------------
    // Native fallback assignments.
    //
    // These prevent input from silently failing when BP_WorldForgePlayerController
    // has blank Class Defaults after reparenting or Blueprint refresh issues.
    // Blueprint defaults can still override these values.
    // ---------------------------------------------------------------------

    static ConstructorHelpers::FObjectFinder<UInputAction> TogglePauseFinder(
        TEXT("/Game/WorldForge/Controls/IA_TogglePause.IA_TogglePause"));

    if (TogglePauseFinder.Succeeded())
    {
        IA_TogglePause = TogglePauseFinder.Object;
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] Native fallback failed to find IA_TogglePause."));
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> ToggleMiniMapFinder(
        TEXT("/Game/WorldForge/Controls/IA_ToggleMiniMap.IA_ToggleMiniMap"));

    if (ToggleMiniMapFinder.Succeeded())
    {
        IA_ToggleMiniMap = ToggleMiniMapFinder.Object;
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] Native fallback failed to find IA_ToggleMiniMap."));
    }

    static ConstructorHelpers::FObjectFinder<UInputMappingContext> BuildMappingFinder(
        TEXT("/Game/WorldForge/Controls/IMC_BuildMode.IMC_BuildMode"));

    if (BuildMappingFinder.Succeeded())
    {
        BuildMappingContext = BuildMappingFinder.Object;
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] Native fallback failed to find IMC_BuildMode."));
    }

    static ConstructorHelpers::FClassFinder<UBuildModeWidget> BuildWidgetFinder(
        TEXT("/Game/WorldForge/WidgetsBlueprints/WBP_BuildMode"));

    if (BuildWidgetFinder.Succeeded())
    {
        BuildWidgetClass = BuildWidgetFinder.Class;
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] Native fallback failed to find WBP_BuildMode."));
    }
}

void AWorldForgePlayerController::BeginPlay()
{
    Super::BeginPlay();

    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);

    if (UEnhancedInputLocalPlayerSubsystem* EIS =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        if (UInputMappingContext* IMC = BuildMappingContext.LoadSynchronous())
        {
            EIS->AddMappingContext(IMC, BuildMappingPriority);

            UE_LOG(
                LogWorldForgePC,
                Log,
                TEXT("[WorldForgePC] IMC_BuildMode added at priority %d"),
                BuildMappingPriority);
        }
        else
        {
            UE_LOG(
                LogWorldForgePC,
                Warning,
                TEXT("[WorldForgePC] BuildMappingContext not set — build input will not work!"));
        }
    }

    BuildModeManager = GetWorld() ? GetWorld()->GetSubsystem<UBuildModeManager>() : nullptr;

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
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] BuildWidgetClass not set."));
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
            EIC->BindAction(
                IA_BuildClick,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::OnBuildClickStarted);

            EIC->BindAction(
                IA_BuildClick,
                ETriggerEvent::Completed,
                this,
                &AWorldForgePlayerController::OnBuildClickReleased);

            EIC->BindAction(
                IA_BuildClick,
                ETriggerEvent::Canceled,
                this,
                &AWorldForgePlayerController::OnBuildClickReleased);
        }

        if (IA_BuildSecondary)
        {
            EIC->BindAction(
                IA_BuildSecondary,
                ETriggerEvent::Triggered,
                this,
                &AWorldForgePlayerController::OnBuildSecondary);
        }

        if (IA_BuildRotate)
        {
            EIC->BindAction(
                IA_BuildRotate,
                ETriggerEvent::Triggered,
                this,
                &AWorldForgePlayerController::OnBuildRotate);
        }

        if (IA_BuildConfirm)
        {
            EIC->BindAction(
                IA_BuildConfirm,
                ETriggerEvent::Triggered,
                this,
                &AWorldForgePlayerController::OnBuildConfirm);
        }

        if (IA_BuildCancel)
        {
            EIC->BindAction(
                IA_BuildCancel,
                ETriggerEvent::Triggered,
                this,
                &AWorldForgePlayerController::OnBuildCancel);
        }

        if (IA_BuildToggle)
        {
            EIC->BindAction(
                IA_BuildToggle,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::OnBuildToggle);
        }

        if (IA_BuildUndo)
        {
            EIC->BindAction(
                IA_BuildUndo,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::OnBuildUndo);
        }

        if (IA_TogglePause)
        {
            EIC->BindAction(
                IA_TogglePause,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::OnPauseToggle);

            UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] IA_TogglePause bound."));
        }
        else
        {
            UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_TogglePause is NOT assigned."));
        }

        if (IA_ToggleMiniMap)
        {
            EIC->BindAction(
                IA_ToggleMiniMap,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::OnMiniMapToggle);

            UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] IA_ToggleMiniMap bound."));
        }
        else
        {
            UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToggleMiniMap is NOT assigned."));
        }

        // Debug/dev-only old dig toggle. Player-facing Mine mode should replace this later.
        if (IA_ToggleDigMode)
        {
            EIC->BindAction(
                IA_ToggleDigMode,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::ToggleDigMode);
        }

        if (IA_ToggleMineMode)
        {
            EIC->BindAction(
                IA_ToggleMineMode,
                ETriggerEvent::Started,
                this,
                &AWorldForgePlayerController::ToggleMineMode);
        }

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Enhanced Input actions bound."));
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] InputComponent is not an EnhancedInputComponent."));
    }

    // ---------------------------------------------------------------------
    // Legacy fallback key binds.
    //
    // These are temporary safety binds while Enhanced Input mappings are
    // being cleaned up. Debounce prevents double-toggles if both paths fire.
    // ---------------------------------------------------------------------
    if (InputComponent)
    {
        InputComponent->BindKey(
            EKeys::SpaceBar,
            IE_Pressed,
            this,
            &AWorldForgePlayerController::OnPauseKeyPressed);

        InputComponent->BindKey(
            EKeys::M,
            IE_Pressed,
            this,
            &AWorldForgePlayerController::OnMiniMapKeyPressed);

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Legacy fallback keys bound: SpaceBar, M."));
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

    if (!ControlledPawn)
    {
        return false;
    }

    AWorldForgeCamera* CameraPawn = Cast<AWorldForgeCamera>(ControlledPawn);

    if (!CameraPawn)
    {
        return false;
    }

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
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const double Now = World->GetTimeSeconds();

    if ((Now - LastBuildClickTime) < BuildClickDebounceSeconds)
    {
        return;
    }

    LastBuildClickTime = Now;
    BuildModeManager->HandlePrimaryClick();
}

void AWorldForgePlayerController::OnBuildClickReleased(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->SetDragBuildActive(false);
}

void AWorldForgePlayerController::OnBuildSecondary(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->HandleSecondaryClick();
}

void AWorldForgePlayerController::OnBuildRotate(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->HandleRotate(true);
}

void AWorldForgePlayerController::OnBuildConfirm(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->HandleConfirm();
}

void AWorldForgePlayerController::OnBuildCancel(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->HandleCancel();
    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::OnBuildToggle(const FInputActionValue& Value)
{
    if (!BuildWidgetInstance)
    {
        return;
    }

    USimulationTimeManager* SimTime = GetSimulationTimeManager();

    if (!SimTime || !SimTime->IsPaused())
    {
        UE_LOG(
            LogWorldForgePC,
            Log,
            TEXT("[WorldForgePC] Build menu blocked. Pause the game first with Space."));

        return;
    }

    SetBuildWidgetVisible(!bBuildWidgetVisible);
}

void AWorldForgePlayerController::OnBuildUndo(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode())
    {
        return;
    }

    BuildModeManager->HandleUndo();
}

void AWorldForgePlayerController::OnPauseToggle(const FInputActionValue& Value)
{
    ExecutePauseToggle();
}

void AWorldForgePlayerController::OnMiniMapToggle(const FInputActionValue& Value)
{
    ExecuteMiniMapToggle();
}

void AWorldForgePlayerController::OnPauseKeyPressed()
{
    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Space fallback pressed."));
    ExecutePauseToggle();
}

void AWorldForgePlayerController::OnMiniMapKeyPressed()
{
    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] M fallback pressed."));
    ExecuteMiniMapToggle();
}

void AWorldForgePlayerController::ExecutePauseToggle()
{
    const UWorld* World = GetWorld();
    const double Now = World ? World->GetRealTimeSeconds() : 0.0;

    if ((Now - LastPauseToggleTime) < ToggleDebounceSeconds)
    {
        UE_LOG(LogWorldForgePC, Verbose, TEXT("[WorldForgePC] Pause toggle ignored by debounce."));
        return;
    }

    LastPauseToggleTime = Now;

    USimulationTimeManager* SimTime = GetSimulationTimeManager();

    if (!SimTime || !SimTime->IsRunning())
    {
        UE_LOG(
            LogWorldForgePC,
            Warning,
            TEXT("[WorldForgePC] Pause toggle ignored: SimulationTimeManager unavailable or not running."));

        return;
    }

    if (SimTime->IsPaused())
    {
        if (bBuildWidgetVisible)
        {
            SetBuildWidgetVisible(false);
        }

        SimTime->ResumeSimulation();

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Space pressed: simulation resumed."));
    }
    else
    {
        SimTime->PauseSimulation();

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Space pressed: simulation paused."));
    }
}

void AWorldForgePlayerController::ExecuteMiniMapToggle()
{
    const UWorld* World = GetWorld();
    const double Now = World ? World->GetRealTimeSeconds() : 0.0;

    if ((Now - LastMiniMapToggleTime) < ToggleDebounceSeconds)
    {
        UE_LOG(LogWorldForgePC, Verbose, TEXT("[WorldForgePC] MiniMap toggle ignored by debounce."));
        return;
    }

    LastMiniMapToggleTime = Now;

    ToggleMiniMapVisibility();
}

USimulationTimeManager* AWorldForgePlayerController::GetSimulationTimeManager() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<USimulationTimeManager>() : nullptr;
}

AWorldForgeMiniMapHUD* AWorldForgePlayerController::GetMiniMapHUD() const
{
    return Cast<AWorldForgeMiniMapHUD>(GetHUD());
}

void AWorldForgePlayerController::SetBuildWidgetVisible(bool bVisible)
{
    if (!BuildWidgetInstance)
    {
        return;
    }

    bBuildWidgetVisible = bVisible;

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
            BuildModeManager->HandleCancel();
        }

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Build widget hidden. Build mode exited."));
    }

    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::ToggleMiniMapVisibility()
{
    AWorldForgeMiniMapHUD* MiniMapHUD = GetMiniMapHUD();

    if (!MiniMapHUD)
    {
        UE_LOG(
            LogWorldForgePC,
            Warning,
            TEXT("[WorldForgePC] MiniMap toggle ignored: current HUD is not AWorldForgeMiniMapHUD."));

        return;
    }

    MiniMapHUD->ToggleMiniMapVisible();
}

// ===================================================================
// DIG MODE
// ===================================================================

void AWorldForgePlayerController::ToggleDigMode()
{
    if (!BuildModeManager)
    {
        return;
    }

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
// MINE MODE
// ===================================================================

void AWorldForgePlayerController::ToggleMineMode()
{
    if (!BuildModeManager)
    {
        return;
    }

    BuildModeManager->EnterMineMode();

    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mine Mode toggled."));
}
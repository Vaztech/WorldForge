// WorldForgePlayerController.cpp
//
// ═══════════════════════════════════════════════════════════════════════════════
// INPUT ARCHITECTURE RULE — READ BEFORE ADDING ANY INPUT
// ═══════════════════════════════════════════════════════════════════════════════
//
// ALL input must go through Enhanced Input Actions. No BindKey. No EKeys::.
//
// When adding a new action:
//   1. Create IA_ asset in /Game/WorldForge/Controls/.
//   2. Add to IMC_BuildMode with a default key.
//   3. Add UPROPERTY in the .h file.
//   4. Add ConstructorHelpers::FObjectFinder below in the constructor
//      (native fallback — keeps it alive through Blueprint refresh).
//   5. Bind in SetupInputComponent via EIC->BindAction.
//   6. If the action should be callable from a UI button, implement the logic
//      in a public Execute* method and have the EI handler call it.
//
// ═══════════════════════════════════════════════════════════════════════════════

#include "WorldForgePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"

#include "Player/BuildModeManager.h"
#include "Player/BuildModeTypes.h"
#include "Blueprint/UserWidget.h"

#include "Debug/WorldForgeCheatManager.h"
#include "WorldForgeCamera.h"
#include "GameFramework/Pawn.h"

#include "Colony/SimulationTimeManager.h"
#include "UI/WorldForgeMiniMapHUD.h"
#include "Visual/TerrainCutawaySubsystem.h"
#include "Colony/ProcessorManager.h"
#include "Colony/WorldForgeColonist.h"
#include "Engine/EngineTypes.h"
#include "Grid/LayerViewSubsystem.h"
#include "Grid/MineLayerCoordinator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldForgePC, Log, All);

// =============================================================================
// Simulation speed configuration
//
// Production time scale: 1 in-game hour = 1 real-world minute at 1x speed.
//   1x → 24 real minutes per game day
//   2x → 12 real minutes per game day
//   3x →  8 real minutes per game day
//   4x →  6 real minutes per game day
//
// These multipliers are passed to SimulationTimeManager::SetSimSpeed().
// Speed levels and default index live here so editing them in one place
// covers keys, UI buttons, and GetCurrentSimSpeedMultiplier() consistently.
// =============================================================================

namespace WorldForgeSimSpeed
{
    static constexpr float  Levels[] = { 1.0f, 2.0f, 3.0f, 4.0f };
    static constexpr int32  LevelCount = 4;
    static constexpr int32  DefaultIndex = 0;  // 1x
}

// =============================================================================
// Constructor — native asset fallbacks only
// =============================================================================

AWorldForgePlayerController::AWorldForgePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    CheatClass = UWorldForgeCheatManager::StaticClass();

    // ── Global ───────────────────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputAction> PauseFinder(
        TEXT("/Game/WorldForge/Controls/IA_TogglePause.IA_TogglePause"));
    if (PauseFinder.Succeeded()) { IA_TogglePause = PauseFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_TogglePause not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> MiniMapFinder(
        TEXT("/Game/WorldForge/Controls/IA_ToggleMiniMap.IA_ToggleMiniMap"));
    if (MiniMapFinder.Succeeded()) { IA_ToggleMiniMap = MiniMapFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToggleMiniMap not found.")); }

    // ── Global mapping context ────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputMappingContext> GlobalIMCFinder(
        TEXT("/Game/WorldForge/Controls/IMC_GlobalControls.IMC_GlobalControls"));
    if (GlobalIMCFinder.Succeeded()) { GlobalMappingContext = GlobalIMCFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IMC_GlobalControls not found.")); }

    // ── Build mapping context ─────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCFinder(
        TEXT("/Game/WorldForge/Controls/IMC_BuildMode.IMC_BuildMode"));
    if (IMCFinder.Succeeded()) { BuildMappingContext = IMCFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IMC_BuildMode not found.")); }

    // ── Tool layer ────────────────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputAction> ToolLayerUpFinder(
        TEXT("/Game/WorldForge/Controls/IA_ToolLayerUp.IA_ToolLayerUp"));
    if (ToolLayerUpFinder.Succeeded()) { IA_ToolLayerUp = ToolLayerUpFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToolLayerUp not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> ToolLayerDownFinder(
        TEXT("/Game/WorldForge/Controls/IA_ToolLayerDown.IA_ToolLayerDown"));
    if (ToolLayerDownFinder.Succeeded()) { IA_ToolLayerDown = ToolLayerDownFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToolLayerDown not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> ToolLayerResetFinder(
        TEXT("/Game/WorldForge/Controls/IA_ToolLayerReset.IA_ToolLayerReset"));
    if (ToolLayerResetFinder.Succeeded()) { IA_ToolLayerReset = ToolLayerResetFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToolLayerReset not found.")); }

    // ── View layer ────────────────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputAction> ViewLayerDownFinder(
        TEXT("/Game/WorldForge/Controls/IA_ViewLayerDown.IA_ViewLayerDown"));
    if (ViewLayerDownFinder.Succeeded()) { IA_ViewLayerDown = ViewLayerDownFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ViewLayerDown not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> ViewLayerUpFinder(
        TEXT("/Game/WorldForge/Controls/IA_ViewLayerUp.IA_ViewLayerUp"));
    if (ViewLayerUpFinder.Succeeded()) { IA_ViewLayerUp = ViewLayerUpFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ViewLayerUp not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> ViewLayerResetFinder(
        TEXT("/Game/WorldForge/Controls/IA_ViewLayerReset.IA_ViewLayerReset"));
    if (ViewLayerResetFinder.Succeeded()) { IA_ViewLayerReset = ViewLayerResetFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ViewLayerReset not found.")); }

    // ── Simulation speed ──────────────────────────────────────────────────────

    static ConstructorHelpers::FObjectFinder<UInputAction> SimSpeedUpFinder(
        TEXT("/Game/WorldForge/Controls/IA_SimSpeedUp.IA_SimSpeedUp"));
    if (SimSpeedUpFinder.Succeeded()) { IA_SimSpeedUp = SimSpeedUpFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_SimSpeedUp not found. Create asset in /Game/WorldForge/Controls/.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> SimSpeedDownFinder(
        TEXT("/Game/WorldForge/Controls/IA_SimSpeedDown.IA_SimSpeedDown"));
    if (SimSpeedDownFinder.Succeeded()) { IA_SimSpeedDown = SimSpeedDownFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_SimSpeedDown not found.")); }

    static ConstructorHelpers::FObjectFinder<UInputAction> SimSpeedResetFinder(
        TEXT("/Game/WorldForge/Controls/IA_SimSpeedReset.IA_SimSpeedReset"));
    if (SimSpeedResetFinder.Succeeded()) { IA_SimSpeedReset = SimSpeedResetFinder.Object; }
    else { UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_SimSpeedReset not found.")); }

    // ── Build widget ──────────────────────────────────────────────────────────
    // Not hard-loaded — assign in BP_WorldForgePlayerController Class Defaults.
    BuildWidgetClass = nullptr;
}

// =============================================================================
// BeginPlay
// =============================================================================

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
        if (UInputMappingContext* GlobalIMC = GlobalMappingContext.LoadSynchronous())
        {
            EIS->AddMappingContext(GlobalIMC, GlobalMappingPriority);
            UE_LOG(LogWorldForgePC, Log,
                TEXT("[WorldForgePC] IMC_GlobalControls added at priority %d"), GlobalMappingPriority);
        }
        else
        {
            UE_LOG(LogWorldForgePC, Warning,
                TEXT("[WorldForgePC] GlobalMappingContext not set — pause/minimap/speed input will not work!"));
        }

        if (UInputMappingContext* BuildIMC = BuildMappingContext.LoadSynchronous())
        {
            EIS->AddMappingContext(BuildIMC, BuildMappingPriority);
            UE_LOG(LogWorldForgePC, Log,
                TEXT("[WorldForgePC] IMC_BuildMode added at priority %d"), BuildMappingPriority);
        }
        else
        {
            UE_LOG(LogWorldForgePC, Warning,
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
        UE_LOG(LogWorldForgePC, Error,
            TEXT("[WorldForgePC] BuildModeManager NOT FOUND — build mode will not function!"));
    }

    MineLayerCoord = GetWorld() ? GetWorld()->GetSubsystem<UMineLayerCoordinator>() : nullptr;

    if (MineLayerCoord)
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] MineLayerCoordinator found and cached."));
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] MineLayerCoordinator NOT FOUND."));
    }

    if (BuildWidgetClass)
    {
        BuildWidgetInstance = CreateWidget<UUserWidget>(this, BuildWidgetClass);

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

// =============================================================================
// SetupInputComponent — one block, all Enhanced Input, no BindKey, no EKeys
// =============================================================================

void AWorldForgePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);

    if (!EIC)
    {
        UE_LOG(LogWorldForgePC, Error,
            TEXT("[WorldForgePC] InputComponent is not an EnhancedInputComponent."));
        return;
    }

    // ── Build mode ────────────────────────────────────────────────────────────

    if (IA_BuildClick)
    {
        EIC->BindAction(IA_BuildClick, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnBuildClickStarted);
        EIC->BindAction(IA_BuildClick, ETriggerEvent::Completed, this,
            &AWorldForgePlayerController::OnBuildClickReleased);
        EIC->BindAction(IA_BuildClick, ETriggerEvent::Canceled, this,
            &AWorldForgePlayerController::OnBuildClickReleased);
    }

    if (IA_BuildSecondary)
        EIC->BindAction(IA_BuildSecondary, ETriggerEvent::Triggered, this,
            &AWorldForgePlayerController::OnBuildSecondary);

    if (IA_BuildRotate)
        EIC->BindAction(IA_BuildRotate, ETriggerEvent::Triggered, this,
            &AWorldForgePlayerController::OnBuildRotate);

    if (IA_BuildConfirm)
        EIC->BindAction(IA_BuildConfirm, ETriggerEvent::Triggered, this,
            &AWorldForgePlayerController::OnBuildConfirm);

    if (IA_BuildCancel)
        EIC->BindAction(IA_BuildCancel, ETriggerEvent::Triggered, this,
            &AWorldForgePlayerController::OnBuildCancel);

    if (IA_BuildToggle)
        EIC->BindAction(IA_BuildToggle, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnBuildToggle);

    if (IA_BuildUndo)
        EIC->BindAction(IA_BuildUndo, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnBuildUndo);

    // ── Tool layer (] [ 0 in IMC_BuildMode) ──────────────────────────────────

    if (IA_ToolLayerUp)
        EIC->BindAction(IA_ToolLayerUp, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnToolLayerUp);

    if (IA_ToolLayerDown)
        EIC->BindAction(IA_ToolLayerDown, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnToolLayerDown);

    if (IA_ToolLayerReset)
        EIC->BindAction(IA_ToolLayerReset, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnToolLayerReset);

    // ── View layer (. , / in IMC_BuildMode) ──────────────────────────────────

    if (IA_ViewLayerDown)
        EIC->BindAction(IA_ViewLayerDown, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnViewLayerDown);

    if (IA_ViewLayerUp)
        EIC->BindAction(IA_ViewLayerUp, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnViewLayerUp);

    if (IA_ViewLayerReset)
        EIC->BindAction(IA_ViewLayerReset, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnViewLayerReset);

    // ── Simulation speed (NumPad+ NumPad- NumPad* in IMC_BuildMode) ───────────

    if (IA_SimSpeedUp)
        EIC->BindAction(IA_SimSpeedUp, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnSimSpeedUp);

    if (IA_SimSpeedDown)
        EIC->BindAction(IA_SimSpeedDown, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnSimSpeedDown);

    if (IA_SimSpeedReset)
        EIC->BindAction(IA_SimSpeedReset, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnSimSpeedReset);

    // ── Global ────────────────────────────────────────────────────────────────

    if (IA_TogglePause)
    {
        EIC->BindAction(IA_TogglePause, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnPauseToggle);

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] IA_TogglePause bound: Started only."));
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_TogglePause not assigned."));
    }

    if (IA_ToggleMiniMap)
    {
        EIC->BindAction(IA_ToggleMiniMap, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::OnMiniMapToggle);
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] IA_ToggleMiniMap bound."));
    }
    else
    {
        UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_ToggleMiniMap not assigned."));
    }

    // ── Debug / tool toggles ─────────────────────────────────────────────────

    if (IA_ToggleDigMode)
        EIC->BindAction(IA_ToggleDigMode, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::ToggleDigMode);

    if (IA_ToggleMineMode)
        EIC->BindAction(IA_ToggleMineMode, ETriggerEvent::Started, this,
            &AWorldForgePlayerController::ToggleMineMode);

    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] All Enhanced Input actions bound."));
}

// =============================================================================
// PlayerTick
// =============================================================================

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

// =============================================================================
// Public Execute* methods — single implementation point per action.
// EI handlers call these. UI buttons call these. One code path.
// =============================================================================

bool AWorldForgePlayerController::SelectColonistByAgentID(int32 AgentID)
{
    if (AgentID < 0)
    {
        ClearSelectedColonist();
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (const UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>())
        {
            if (ProcessorManager->GetAgent(AgentID))
            {
                SelectedAgentID = AgentID;
                UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Selected colonist AgentID=%d"), SelectedAgentID);
                return true;
            }
        }
    }

    return false;
}

bool AWorldForgePlayerController::SelectColonistActor(AWorldForgeColonist* ColonistActor)
{
    if (!IsValid(ColonistActor))
    {
        ClearSelectedColonist();
        return false;
    }

    return SelectColonistByAgentID(ColonistActor->AgentID);
}

bool AWorldForgePlayerController::SelectColonistUnderCursor()
{
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit))
    {
        ClearSelectedColonist();
        return false;
    }

    AActor* HitActor = Hit.GetActor();
    while (HitActor)
    {
        if (AWorldForgeColonist* Colonist = Cast<AWorldForgeColonist>(HitActor))
        {
            return SelectColonistActor(Colonist);
        }

        HitActor = HitActor->GetAttachParentActor();
    }

    ClearSelectedColonist();
    return false;
}

void AWorldForgePlayerController::ClearSelectedColonist()
{
    SelectedAgentID = INDEX_NONE;
}

bool AWorldForgePlayerController::GetSelectedColonistInspectionData(FColonistInspectionData& OutData) const
{
    if (SelectedAgentID < 0)
    {
        OutData = FColonistInspectionData();
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (const UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>())
        {
            return ProcessorManager->GetAgentInspectionData(SelectedAgentID, OutData);
        }
    }

    OutData = FColonistInspectionData();
    return false;
}

bool AWorldForgePlayerController::SetSelectedColonistJobPriority(FGameplayTag JobTag, int32 Priority)
{
    if (SelectedAgentID < 0)
    {
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>())
        {
            return ProcessorManager->SetAgentJobPriority(SelectedAgentID, JobTag, Priority);
        }
    }

    return false;
}

void AWorldForgePlayerController::ExecuteToolLayerAdjust(int32 Delta)
{
    if (BuildModeManager && BuildModeManager->IsInBuildMode())
    {
        const bool bIsMineMode = BuildModeManager->IsInMineMode();

        if (bIsMineMode)
        {
            // MineLayerCoordinator is the single owner of mine-mode layer changes.
            // It calls GridSelectionSubsystem::AdjustActiveToolLayer (which moves LayerView),
            // then atomically syncs TerrainCutawaySubsystem. Never do these separately.
            if (MineLayerCoord)
            {
                MineLayerCoord->MoveLayer(Delta);
            }

            UE_LOG(LogWorldForgePC, Log,
                TEXT("[WorldForgePC] Mine layer moved via coordinator. Delta=%d VoxelZ=%d"),
                Delta,
                MineLayerCoord ? MineLayerCoord->GetActiveVoxelLayerZ() : 0);
        }
        else
        {
            BuildModeManager->AdjustActiveToolLayer(Delta);
        }
    }
}

void AWorldForgePlayerController::ExecuteToolLayerReset()
{
    if (BuildModeManager && BuildModeManager->IsInBuildMode())
    {
        const bool bIsMineMode = BuildModeManager->IsInMineMode();

        if (bIsMineMode)
        {
            if (MineLayerCoord)
            {
                MineLayerCoord->ResetToSurface();
            }

            UE_LOG(LogWorldForgePC, Log,
                TEXT("[WorldForgePC] Mine layer reset to surface via coordinator."));
        }
        else
        {
            BuildModeManager->ResetActiveToolLayer();
        }
    }
}

void AWorldForgePlayerController::ExecuteViewLayerAdjust(int32 Delta)
{
    if (BuildModeManager && BuildModeManager->IsInMineMode())
    {
        ExecuteToolLayerAdjust(Delta);
        return;
    }

    if (UWorld* W = GetWorld())
    {
        if (UTerrainCutawaySubsystem* Cutaway = W->GetSubsystem<UTerrainCutawaySubsystem>())
        {
            Cutaway->AdjustViewLayer(Delta);
        }
    }
}

void AWorldForgePlayerController::ExecuteViewLayerReset()
{
    if (BuildModeManager && BuildModeManager->IsInMineMode())
    {
        ExecuteToolLayerReset();
        return;
    }

    if (UWorld* W = GetWorld())
    {
        if (UTerrainCutawaySubsystem* Cutaway = W->GetSubsystem<UTerrainCutawaySubsystem>())
        {
            Cutaway->ResetViewLayer();
        }
    }
}

// =============================================================================
// ExecutePauseToggle
//
// FIX: Removed IsRunning() guard. That guard prevented pause from working
// in cases where IsRunning() returned an unexpected state. IsPaused() is the
// only runtime condition that matters here — SimulationTimeManager guards its
// own internal state inside PauseSimulation/ResumeSimulation.
// =============================================================================

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

    if (!SimTime)
    {
        UE_LOG(LogWorldForgePC, Warning,
            TEXT("[WorldForgePC] Pause toggle failed: SimulationTimeManager not found."));
        return;
    }

    if (SimTime->IsPaused())
    {
        if (bBuildWidgetVisible)
        {
            SetBuildWidgetVisible(false);
        }

        SimTime->ResumeSimulation();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Simulation resumed at %dx speed."),
            CurrentSimSpeedIndex + 1);
    }
    else
    {
        SimTime->PauseSimulation();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Simulation paused."));
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

void AWorldForgePlayerController::ExecuteToggleMineMode()
{
    ToggleMineMode();
}

void AWorldForgePlayerController::ExecuteExitMineModeIfActive()
{
    // Mine mode is split across BuildModeManager plus controller-owned camera,
    // terrain cutaway, and mine-layer coordinator state.  Any non-mine tool
    // switch must clear all of that state first, otherwise the next layer move
    // can re-enter cutaway at an invalid Z such as 0.0.
    const bool bWasInMineMode = BuildModeManager && BuildModeManager->IsInMineMode();

    if (bWasInMineMode)
    {
        BuildModeManager->HandleCancel();
    }

    // Safe even if not active; ExitMiningSubsystems checks each subsystem.
    ExitMiningSubsystems();
    RefreshBuildInputSuppression();

    if (bWasInMineMode)
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mine Mode force-exited before tool switch."));
    }
}

// =============================================================================
// Simulation speed Execute* methods
// =============================================================================

void AWorldForgePlayerController::ExecuteSimSpeedUp()
{
    const int32 NewIndex = FMath::Clamp(
        CurrentSimSpeedIndex + 1, 0, WorldForgeSimSpeed::LevelCount - 1);

    if (NewIndex == CurrentSimSpeedIndex)
    {
        UE_LOG(LogWorldForgePC, Log,
            TEXT("[WorldForgePC] Sim speed already at maximum (%dx)."),
            static_cast<int32>(WorldForgeSimSpeed::Levels[CurrentSimSpeedIndex]));
        return;
    }

    CurrentSimSpeedIndex = NewIndex;
    ApplyCurrentSimSpeed();
}

void AWorldForgePlayerController::ExecuteSimSpeedDown()
{
    const int32 NewIndex = FMath::Clamp(
        CurrentSimSpeedIndex - 1, 0, WorldForgeSimSpeed::LevelCount - 1);

    if (NewIndex == CurrentSimSpeedIndex)
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Sim speed already at minimum (1x)."));
        return;
    }

    CurrentSimSpeedIndex = NewIndex;
    ApplyCurrentSimSpeed();
}

void AWorldForgePlayerController::ExecuteSimSpeedReset()
{
    if (CurrentSimSpeedIndex == WorldForgeSimSpeed::DefaultIndex)
    {
        UE_LOG(LogWorldForgePC, Verbose,
            TEXT("[WorldForgePC] Sim speed already at 1x."));
        return;
    }

    CurrentSimSpeedIndex = WorldForgeSimSpeed::DefaultIndex;
    ApplyCurrentSimSpeed();
}

float AWorldForgePlayerController::GetCurrentSimSpeedMultiplier() const
{
    return WorldForgeSimSpeed::Levels[CurrentSimSpeedIndex];
}

// =============================================================================
// ApplyCurrentSimSpeed
//
// Writes the active speed level to SimulationTimeManager::SetSimSpeed().
// The method is confirmed as SetSimSpeed(float) in SimulationTimeManager.h.
// =============================================================================

void AWorldForgePlayerController::ApplyCurrentSimSpeed()
{
    USimulationTimeManager* SimTime = GetSimulationTimeManager();

    if (!SimTime)
    {
        UE_LOG(LogWorldForgePC, Warning,
            TEXT("[WorldForgePC] ApplyCurrentSimSpeed: SimulationTimeManager not found."));
        return;
    }

    const float Multiplier = WorldForgeSimSpeed::Levels[CurrentSimSpeedIndex];

    SimTime->SetSimSpeed(Multiplier);

    UE_LOG(LogWorldForgePC, Log,
        TEXT("[WorldForgePC] Sim speed → %dx (%.1fx). "
            "1 in-game hour = %.0f real seconds at this speed."),
        CurrentSimSpeedIndex + 1,
        Multiplier,
        60.0f / Multiplier);
}

// =============================================================================
// Enhanced Input handlers — forward to Execute* only, no logic here
// =============================================================================

void AWorldForgePlayerController::OnBuildClickStarted(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;

    const UWorld* World = GetWorld();
    if (!World) return;

    const double Now = World->GetTimeSeconds();
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

    const bool bWasInMineMode = BuildModeManager->IsInMineMode();
    BuildModeManager->HandleCancel();
    RefreshBuildInputSuppression();

    if (bWasInMineMode)
    {
        ExitMiningSubsystems();
    }
}

void AWorldForgePlayerController::OnBuildToggle(const FInputActionValue& Value)
{
    if (!BuildWidgetInstance) return;

    USimulationTimeManager* SimTime = GetSimulationTimeManager();
    if (!SimTime || !SimTime->IsPaused())
    {
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Build menu blocked. Pause first."));
        return;
    }

    SetBuildWidgetVisible(!bBuildWidgetVisible);
}

void AWorldForgePlayerController::OnBuildUndo(const FInputActionValue& Value)
{
    if (!BuildModeManager || !BuildModeManager->IsInBuildMode()) return;
    BuildModeManager->HandleUndo();
}

void AWorldForgePlayerController::OnToolLayerUp() { ExecuteToolLayerAdjust(+1); }
void AWorldForgePlayerController::OnToolLayerDown() { ExecuteToolLayerAdjust(-1); }
void AWorldForgePlayerController::OnToolLayerReset() { ExecuteToolLayerReset(); }

void AWorldForgePlayerController::OnViewLayerDown() { ExecuteViewLayerAdjust(-1); }
void AWorldForgePlayerController::OnViewLayerUp() { ExecuteViewLayerAdjust(+1); }
void AWorldForgePlayerController::OnViewLayerReset() { ExecuteViewLayerReset(); }

void AWorldForgePlayerController::OnSimSpeedUp() { ExecuteSimSpeedUp(); }
void AWorldForgePlayerController::OnSimSpeedDown() { ExecuteSimSpeedDown(); }
void AWorldForgePlayerController::OnSimSpeedReset() { ExecuteSimSpeedReset(); }

void AWorldForgePlayerController::OnPauseToggle(const FInputActionValue& Value)
{
    UE_LOG(LogWorldForgePC, Warning, TEXT("[WorldForgePC] IA_TogglePause fired."));
    ExecutePauseToggle();
}

void AWorldForgePlayerController::OnMiniMapToggle(const FInputActionValue& Value)
{
    ExecuteMiniMapToggle();
}

// =============================================================================
// Dig / Mine mode
// =============================================================================

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

void AWorldForgePlayerController::ToggleMineMode()
{
    if (!BuildModeManager) return;

    const bool bWasInMineMode = BuildModeManager->IsInMineMode();

    BuildModeManager->EnterMineMode();

    if (bWasInMineMode)
    {
        ExitMiningSubsystems();
        RefreshBuildInputSuppression();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mine Mode exited."));
        return;
    }

    if (BuildModeManager->IsInMineMode())
    {
        EnterMiningSubsystems();
        RefreshBuildInputSuppression();
        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mine Mode entered."));
    }
}

// =============================================================================
// Mining subsystem helpers
// =============================================================================

void AWorldForgePlayerController::EnterMiningSubsystems()
{
    float LayerWorldZ = 0.0f;
    bool bResolvedLayerZ = false;

    if (UWorld* W = GetWorld())
    {
        if (ULayerViewSubsystem* LayerView = W->GetSubsystem<ULayerViewSubsystem>())
        {
            if (LayerView->HasActiveVoxelLayer())
            {
                LayerWorldZ = LayerView->GetActiveLayerWorldZ();
                bResolvedLayerZ = true;
            }
        }
    }

    // Fallback: if LayerView has not published an active layer yet, derive a
    // sane mine surface Z from the build manager's active voxel layer.  Do not
    // silently fall back to 0.0 unless the actual layer is 0; doing so corrupts
    // cutaway/mine-layer state and can visually throw the camera under the map.
    if (!bResolvedLayerZ && BuildModeManager)
    {
        const int32 ActiveVoxelZ = BuildModeManager->GetActiveVoxelLayerZ();
        if (ActiveVoxelZ != 0)
        {
            LayerWorldZ = static_cast<float>(ActiveVoxelZ) * 100.0f;
            bResolvedLayerZ = true;
        }
    }

    // Last fallback: use the current camera pivot height so cutaway starts near
    // the player's view instead of world zero.
    if (!bResolvedLayerZ && WorldForgeCamera)
    {
        LayerWorldZ = WorldForgeCamera->GetActorLocation().Z;
        bResolvedLayerZ = true;
    }

    if (MineLayerCoord)
    {
        MineLayerCoord->InitializeForMineMode(LayerWorldZ);
    }

    if (WorldForgeCamera)
    {
        WorldForgeCamera->EnterMiningCameraMode(LayerWorldZ);
    }

    UE_LOG(LogWorldForgePC, Log,
        TEXT("[WorldForgePC] Mining subsystems entered. LayerWorldZ=%.1f Resolved=%s"),
        LayerWorldZ,
        bResolvedLayerZ ? TEXT("true") : TEXT("false"));
}

void AWorldForgePlayerController::ExitMiningSubsystems()
{
    if (UWorld* W = GetWorld())
    {
        if (UTerrainCutawaySubsystem* Cutaway = W->GetSubsystem<UTerrainCutawaySubsystem>())
        {
            if (Cutaway->IsCutawayActive())
            {
                Cutaway->ExitCutawayMode();
            }
        }
    }

    if (WorldForgeCamera)
    {
        WorldForgeCamera->ExitMiningCameraMode();
    }

    UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Mining subsystems exited."));
}

// =============================================================================
// State helpers
// =============================================================================

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

    if (bShouldSuppress == bBuildInputSuppressed) return;

    bBuildInputSuppressed = bShouldSuppress;
    SetIgnoreMoveInput(false);

    UE_LOG(LogWorldForgePC, Log,
        TEXT("[WorldForgePC] Camera input refreshed. InBuildMode=%s Suppressed=%s"),
        (BuildModeManager && BuildModeManager->IsInBuildMode()) ? TEXT("true") : TEXT("false"),
        bBuildInputSuppressed ? TEXT("true") : TEXT("false"));
}

bool AWorldForgePlayerController::AcquireAndApplyCameraViewTarget(bool bLogResult)
{
    AWorldForgeCamera* CameraPawn = Cast<AWorldForgeCamera>(GetPawn());
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
    if (!BuildWidgetInstance) return;

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
            ExitMiningSubsystems();
        }

        UE_LOG(LogWorldForgePC, Log, TEXT("[WorldForgePC] Build widget hidden."));
    }

    RefreshBuildInputSuppression();
}

void AWorldForgePlayerController::ToggleMiniMapVisibility()
{
    AWorldForgeMiniMapHUD* MiniMapHUD = GetMiniMapHUD();

    if (!MiniMapHUD)
    {
        UE_LOG(LogWorldForgePC, Warning,
            TEXT("[WorldForgePC] MiniMap toggle ignored: HUD is not AWorldForgeMiniMapHUD."));
        return;
    }

    MiniMapHUD->ToggleMiniMapVisible();
}

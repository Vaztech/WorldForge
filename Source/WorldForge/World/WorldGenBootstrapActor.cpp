#include "WorldGenBootstrapActor.h"

#include "WorldForgeLakeSurfaceActor.h"
#include "WorldForgeRiverSurfaceActor.h"
#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldMapSubsystem.h"
#include "World/WorldDecorationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Voxel/VoxelWorldManager.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/WorldForgeCamera.h"
#include "TimerManager.h"
#include "UI/WorldForgeEmbarkationManager.h"
#include "UI/WorldForgeMiniMapHUD.h"
#include "Civilization/CivilizationActivitySubsystem.h"
#include "UObject/UnrealType.h"
#include "Player/WorldForgePlayerController.h"

namespace
{
    static constexpr float VoxelMeshReadyPollIntervalSeconds = 0.5f;
    static constexpr float VoxelMeshReadyMaxWaitSeconds = 60.0f;
    static constexpr int32 VoxelMeshReadyTaskThreshold = 0;
    static constexpr float MinTerrainLoadGateSeconds = 3.0f;
    static constexpr int32 RequiredReadyPolls = 3;

    static const TCHAR* const WaterlineLakePoolClassPath =
        TEXT("/Game/Waterline/5_Lake/BP_Lake_2.BP_Lake_2_C");

    static const TCHAR* const WaterlineLakeLightMeshPath =
        TEXT("/Game/Waterline/3_Ocean_Sim_Content/1_BP_Actors/2_Models/NewGeometry/2_SM_UE5_SS_GRID_Lake.2_SM_UE5_SS_GRID_Lake");

    static const TCHAR* const WaterlineLakeLightFallbackMeshPath =
        TEXT("/Engine/BasicShapes/Plane.Plane");

    static const TCHAR* const WaterlineLakeLightMaterialPath =
        TEXT("/Game/WorldForge/Materials/Water/MI_WF_InlandLake.MI_WF_InlandLake");

    static const TCHAR* const WaterlineRiverLightMeshPath =
        TEXT("/Game/Waterline/3_Ocean_Sim_Content/1_BP_Actors/2_Models/NewGeometry/2_SM_UE5_SS_GRID_Lake.2_SM_UE5_SS_GRID_Lake");

    static const TCHAR* const WaterlineRiverLightFallbackMeshPath =
        TEXT("/Engine/BasicShapes/Plane.Plane");

    static const TCHAR* const WaterlineRiverLightMaterialPath =
        TEXT("/Game/WorldForge/Materials/Water/MI_WF_InlandLake.MI_WF_InlandLake");

    static constexpr float WaterlineLakeUnusedStagingZ = -999999.0f;

    bool DoesBiomeMatch(const FGameplayTag& BiomeTag, const TCHAR* TagName)
    {
        if (!BiomeTag.IsValid() || !TagName)
        {
            return false;
        }

        const FGameplayTag MatchTag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
        return MatchTag.IsValid() && BiomeTag.MatchesTag(MatchTag);
    }

    FString GetWaterlineLakeBiomeString(const FGameplayTag& BiomeTag)
    {
        return BiomeTag.IsValid() ? BiomeTag.ToString() : FString(TEXT("Invalid"));
    }

    bool IsAllowedLightweightLakeBiome(const FGameplayTag& BiomeTag)
    {
        // Ocean/sea-like biomes are intentionally excluded here because the descriptor
        // does not currently distinguish inland saltwater from open/coastal water.
        return
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Lake")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Water")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Freshwater")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Wetland")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Marsh")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.Swamp")) ||
            DoesBiomeMatch(BiomeTag, TEXT("WorldForge.Biome.River"));
    }

    const TCHAR* ValidateLightweightLakeDesc(const FWaterlineLakeSurfaceDesc& Desc)
    {
        if (Desc.HalfExtentX <= 0.0f || Desc.HalfExtentY <= 0.0f)
        {
            return TEXT("InvalidExtent");
        }

        if (!FMath::IsFinite(Desc.WaterSurfaceZ))
        {
            return TEXT("InvalidWaterZ");
        }

        if (DoesBiomeMatch(Desc.BiomeTag, TEXT("WorldForge.Biome.Lava")))
        {
            return TEXT("LavaBiome");
        }

        if (DoesBiomeMatch(Desc.BiomeTag, TEXT("WorldForge.Biome.Volcanic")))
        {
            return TEXT("VolcanicBiome");
        }

        if (Desc.TileCount < 3)
        {
            return TEXT("TooFewTiles");
        }

        if (!IsAllowedLightweightLakeBiome(Desc.BiomeTag))
        {
            return TEXT("NonWaterBiome");
        }

        return nullptr;
    }

    void LogLightweightLakeSkip(const FWaterlineLakeSurfaceDesc& Desc, const TCHAR* Reason)
    {
        const FString BiomeString = GetWaterlineLakeBiomeString(Desc.BiomeTag);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[WaterlineLakeLight] Skipped Basin=%d Reason=%s Biome=%s Tiles=%d HalfExtent=(%.0f,%.0f) WaterZ=%.1f"),
            Desc.BasinID,
            Reason ? Reason : TEXT("Unknown"),
            *BiomeString,
            Desc.TileCount,
            Desc.HalfExtentX,
            Desc.HalfExtentY,
            Desc.WaterSurfaceZ);
    }

    const TCHAR* ValidateLightweightRiverDesc(const FWorldForgeRiverSurfaceDesc& Desc)
    {
        if (Desc.TileCount < 2 || Desc.TilePath.Num() < 2)
        {
            return TEXT("TooFewTiles");
        }

        if (!FMath::IsFinite(Desc.AverageWaterZ))
        {
            return TEXT("InvalidWaterZ");
        }

        if (Desc.AverageWidth <= 0.0f)
        {
            return TEXT("InvalidWidth");
        }

        return nullptr;
    }

    void LogLightweightRiverSkip(const FWorldForgeRiverSurfaceDesc& Desc, const TCHAR* Reason)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[WaterlineRiverLight] Skipped River=%d Reason=%s Tiles=%d Width=%.2f AvgZ=%.1f"),
            Desc.RiverID,
            Reason ? Reason : TEXT("Unknown"),
            Desc.TileCount,
            Desc.AverageWidth,
            Desc.AverageWaterZ);
    }

    FProperty* FindWaterlineProperty(AActor* Actor, const TCHAR* VariableName)
    {
        if (!IsValid(Actor) || !VariableName)
        {
            return nullptr;
        }

        UClass* ActorClass = Actor->GetClass();
        if (!ActorClass)
        {
            return nullptr;
        }

        if (FProperty* Property = ActorClass->FindPropertyByName(FName(VariableName)))
        {
            return Property;
        }

        FString SanitizedName(VariableName);
        SanitizedName.ReplaceInline(TEXT(" "), TEXT("_"));
        if (FProperty* Property = ActorClass->FindPropertyByName(FName(*SanitizedName)))
        {
            return Property;
        }

        FString CompactedName(VariableName);
        CompactedName.ReplaceInline(TEXT(" "), TEXT(""));
        return ActorClass->FindPropertyByName(FName(*CompactedName));
    }

    bool IsWaterlineNumericVariableSettable(AActor* Actor, const TCHAR* VariableName)
    {
        FProperty* Property = FindWaterlineProperty(Actor, VariableName);
        return CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property);
    }

    bool SetWaterlineNumericVariable(AActor* Actor, const TCHAR* VariableName, double Value)
    {
        FProperty* Property = FindWaterlineProperty(Actor, VariableName);
        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            FloatProperty->SetPropertyValue_InContainer(Actor, static_cast<float>(Value));
            return true;
        }

        if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            DoubleProperty->SetPropertyValue_InContainer(Actor, Value);
            return true;
        }

        return false;
    }

    bool SetWaterlineBoolVariable(AActor* Actor, const TCHAR* VariableName, bool bValue)
    {
        FProperty* Property = FindWaterlineProperty(Actor, VariableName);
        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            BoolProperty->SetPropertyValue_InContainer(Actor, bValue);
            return true;
        }

        return false;
    }

    void SetWaterlineLakeRuntimeFlags(AActor* LakeActor, bool bActorActive)
    {
        SetWaterlineBoolVariable(LakeActor, TEXT("Actor Active"), bActorActive);
        SetWaterlineBoolVariable(LakeActor, TEXT("Enable Shallow Water Sim"), false);
        SetWaterlineBoolVariable(LakeActor, TEXT("Use Underwater VFX"), false);

        if (IsValid(LakeActor))
        {
            LakeActor->SetActorHiddenInGame(!bActorActive);
            LakeActor->SetActorEnableCollision(bActorActive);
        }
    }

    AWorldForgeCamera* FindWorldForgeCamera(UWorld* World, APlayerController* PlayerController)
    {
        if (PlayerController)
        {
            if (AWorldForgeCamera* CameraPawn = Cast<AWorldForgeCamera>(PlayerController->GetPawn()))
            {
                return CameraPawn;
            }

            if (AWorldForgeCamera* ViewTargetCamera = Cast<AWorldForgeCamera>(PlayerController->GetViewTarget()))
            {
                return ViewTargetCamera;
            }
        }

        if (!World)
        {
            return nullptr;
        }

        for (TActorIterator<AWorldForgeCamera> It(World); It; ++It)
        {
            if (AWorldForgeCamera* CameraPawn = *It)
            {
                if (IsValid(CameraPawn))
                {
                    return CameraPawn;
                }
            }
        }

        return nullptr;
    }

    void ResolveCameraHandoffView(
        const AWorldForgeCamera* CameraPawn,
        const APlayerController* PlayerController,
        float& OutYaw,
        float& OutPitch,
        float& OutZoom)
    {
        const FRotator ControlRotation = PlayerController ? PlayerController->GetControlRotation() : FRotator::ZeroRotator;

        OutYaw = ControlRotation.Yaw;
        OutPitch = ControlRotation.Pitch;
        OutZoom = 3000.0f;

        if (CameraPawn && CameraPawn->CameraArm)
        {
            const FRotator ArmRotation = CameraPawn->CameraArm->GetComponentRotation();
            OutYaw = ArmRotation.Yaw;
            OutPitch = ArmRotation.Pitch;
            OutZoom = CameraPawn->CameraArm->TargetArmLength;
        }

        if (FMath::IsNearlyZero(OutZoom))
        {
            OutZoom = 3000.0f;
        }
    }

    void RequestNavigatorCompassShowForGameplay(UWorld* World)
    {
        if (!World)
        {
            return;
        }

        APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
        if (!PlayerController)
        {
            return;
        }

    static const FName ShowCompassFunctionName(TEXT("ShowNavigatorCompassForGameplay"));
    if (UFunction* ShowCompassFunction = PlayerController->FindFunction(ShowCompassFunctionName))
    {
        PlayerController->ProcessEvent(ShowCompassFunction, nullptr);
        UE_LOG(LogTemp, Log, TEXT("[NavigatorHUD] Compass shown for gameplay."));
    }
    }

    void RequestNavigatorCompassHideForMapUI(UWorld* World)
    {
        if (!World)
        {
            return;
        }

        APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
        if (!PlayerController)
        {
            return;
        }

    static const FName HideCompassFunctionName(TEXT("HideNavigatorCompassForMapUI"));
    if (UFunction* HideCompassFunction = PlayerController->FindFunction(HideCompassFunctionName))
    {
        PlayerController->ProcessEvent(HideCompassFunction, nullptr);
        UE_LOG(LogTemp, Log, TEXT("[NavigatorHUD] Compass hidden for map UI."));
    }
    }

    AWorldForgeMiniMapHUD* ResolveWorldForgeMiniMapHUD(UWorld* World)
    {
        if (!World)
        {
            return nullptr;
        }

        if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
        {
            if (AWorldForgeMiniMapHUD* MiniMapHUD = Cast<AWorldForgeMiniMapHUD>(PlayerController->GetHUD()))
            {
                return MiniMapHUD;
            }
        }

        for (TActorIterator<AWorldForgeMiniMapHUD> It(World); It; ++It)
        {
            if (IsValid(*It))
            {
                return *It;
            }
        }

        return nullptr;
    }

    void SetLegacyHudSuppressedForFullMap(UWorld* World, bool bSuppress)
    {
        if (AWorldForgeMiniMapHUD* MiniMapHUD = ResolveWorldForgeMiniMapHUD(World))
        {
            MiniMapHUD->SetLegacyHudSuppressedForFullMap(bSuppress);
        }
    }
}

AWorldGenBootstrapActor::AWorldGenBootstrapActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AWorldGenBootstrapActor::QueueSelectedTileCameraHandoff(int32 TileX, int32 TileY)
{
    bFullMapConfirmed = true;
    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Full map confirmed; entering terrain loading state."));

    PendingCameraHandoffTile = FIntPoint(TileX, TileY);
    PendingCameraHandoffWorldLocation = FVector::ZeroVector;
    bPendingCameraHandoffWorldLocationPreResolved = false;
    bPendingSelectedTileCameraHandoff = true;

    UE_LOG(LogTemp, Log, TEXT("[CameraHandoff] Queued SelectedTile=(%d,%d)"), TileX, TileY);
    UE_LOG(LogTemp, Log, TEXT("[CameraHandoff] Legacy tile-to-world resolve used. Tile=(%d,%d)"), TileX, TileY);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);
        CameraHandoffPollStartTimeSeconds = 0.0f;

        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (bDecorationPassDispatched && VoxelWorldManager->IsSurfaceReady())
            {
                ProcessPendingSelectedTileCameraHandoff();
            }
        }
    }
}

void AWorldGenBootstrapActor::QueueSelectedTileCameraHandoff(int32 TileX, int32 TileY, const FVector& ResolvedSpawnWorldLocation)
{
    bFullMapConfirmed = true;
    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Full map confirmed; entering terrain loading state."));

    PendingCameraHandoffTile = FIntPoint(TileX, TileY);
    PendingCameraHandoffWorldLocation = ResolvedSpawnWorldLocation;
    bPendingCameraHandoffWorldLocationPreResolved = true;
    bPendingSelectedTileCameraHandoff = true;

    UE_LOG(LogTemp, Log,
        TEXT("[CameraHandoff] Queued SelectedTile=(%d,%d) World=(X=%.1f,Y=%.1f,Z=%.1f) Source=FullMapResolvedSpawn"),
        TileX, TileY,
        ResolvedSpawnWorldLocation.X,
        ResolvedSpawnWorldLocation.Y,
        ResolvedSpawnWorldLocation.Z);
    UE_LOG(LogTemp, Log, TEXT("[CameraHandoff] Using resolved spawn world location for CubePrototype."));

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);
        CameraHandoffPollStartTimeSeconds = 0.0f;

        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (bDecorationPassDispatched && VoxelWorldManager->IsSurfaceReady())
            {
                ProcessPendingSelectedTileCameraHandoff();
            }
        }
    }
}

void AWorldGenBootstrapActor::BeginPlay()
{
    Super::BeginPlay();

    if (bGenerateOnBeginPlay)
    {
        RunBootstrap();
    }
}

void AWorldGenBootstrapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);

        if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (bBoundToTerrainReady)
            {
                VM->OnTerrainReady.RemoveDynamic(this, &AWorldGenBootstrapActor::HandleTerrainReady);
                bBoundToTerrainReady = false;
            }
        }
    }

    Super::EndPlay(EndPlayReason);
}

bool AWorldGenBootstrapActor::ResolvePendingCameraHandoffWorldLocation(FVector& OutWorldLocation) const
{
    OutWorldLocation = FVector::ZeroVector;

    if (PendingCameraHandoffTile.X == INDEX_NONE || PendingCameraHandoffTile.Y == INDEX_NONE)
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelWorldManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TerrainMeshReady] WARNING: VoxelWorldManager missing during mesh warm-up gate."));
        return false;
    }

    if (VoxelWorldManager->TryGetSurfaceWorldLocation(
        PendingCameraHandoffTile,
        OutWorldLocation,
        VoxelWorldManager->TerrainSurfaceZOffset))
    {
        return true;
    }

    OutWorldLocation = VoxelWorldManager->TerrainCellToWorld(PendingCameraHandoffTile, 0.0f, true);
    return true;
}

void AWorldGenBootstrapActor::ClearPendingSelectedTileCameraHandoff()
{
    bPendingSelectedTileCameraHandoff = false;
    PendingCameraHandoffTile = FIntPoint(INDEX_NONE, INDEX_NONE);
    PendingCameraHandoffWorldLocation = FVector::ZeroVector;
    bPendingCameraHandoffWorldLocationPreResolved = false;
    CameraHandoffPollStartTimeSeconds = 0.0f;
    ConsecutiveReadyPolls = 0;
    LastPollTotalTasks = -1;
    LastPollMeshTasks = -1;
}

void AWorldGenBootstrapActor::ReleasePrePlayBlock()
{
    bWorldForgePrePlayBlocking = false;
    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Play view enabled."));

    if (PendingFullMapWidgetInstance)
    {
        if (PendingFullMapWidgetInstance->IsInViewport())
        {
            PendingFullMapWidgetInstance->RemoveFromParent();
        }

        PendingFullMapWidgetInstance = nullptr;
    }

    if (UWorld* World = GetWorld())
    {
        SetLegacyHudSuppressedForFullMap(World, false);

        if (AWorldForgePlayerController* WFPC = Cast<AWorldForgePlayerController>(World->GetFirstPlayerController()))
        {
            WFPC->NotifyCameraHandoffComplete();
        }

        RequestNavigatorCompassShowForGameplay(World);
    }

    if (bDestroyAfterBootstrap)
    {
        Destroy();
    }
}

void AWorldGenBootstrapActor::ProcessPendingSelectedTileCameraHandoff()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);

    if (!bPendingSelectedTileCameraHandoff ||
        PendingCameraHandoffTile.X == INDEX_NONE ||
        PendingCameraHandoffTile.Y == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TerrainMeshReady] WARNING: No pending selected tile; using fallback release."));
        ClearPendingSelectedTileCameraHandoff();
        ReleasePrePlayBlock();
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[CameraHandoff] TerrainReady processing queued camera tile=(%d,%d)"),
        PendingCameraHandoffTile.X,
        PendingCameraHandoffTile.Y);

    if (!bPendingCameraHandoffWorldLocationPreResolved)
    {
        if (!ResolvePendingCameraHandoffWorldLocation(PendingCameraHandoffWorldLocation))
        {
            ClearPendingSelectedTileCameraHandoff();
            ReleasePrePlayBlock();
            return;
        }
    }

    if (APlayerController* PlayerController = World->GetFirstPlayerController())
    {
        if (AWorldForgeCamera* CameraPawn = FindWorldForgeCamera(World, PlayerController))
        {
            float CameraYaw = 0.0f;
            float CameraPitch = 0.0f;
            float CameraZoom = 0.0f;
            ResolveCameraHandoffView(CameraPawn, PlayerController, CameraYaw, CameraPitch, CameraZoom);
            CameraPawn->SnapToRTSView(PendingCameraHandoffWorldLocation, CameraYaw, CameraPitch, CameraZoom, true);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[CameraHandoff] WARNING: No camera pawn found."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[CameraHandoff] WARNING: No camera pawn found."));
    }

    CameraHandoffPollStartTimeSeconds = World->GetTimeSeconds();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelMeshReady] Poll started Tile=(%d,%d) MaxWait=%.1f PollInterval=%.1f"),
        PendingCameraHandoffTile.X,
        PendingCameraHandoffTile.Y,
        VoxelMeshReadyMaxWaitSeconds,
        VoxelMeshReadyPollIntervalSeconds);

    PollVoxelMeshReady();
}

void AWorldGenBootstrapActor::PollVoxelMeshReady()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);

    const float ElapsedSeconds = FMath::Max(0.0f, World->GetTimeSeconds() - CameraHandoffPollStartTimeSeconds);

    const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    FWorldForgeVoxelMeshBacklog Backlog;
    const bool bBacklogAvailable = VoxelWorldManager && VoxelWorldManager->TryGetVoxelMeshBacklog(Backlog);

    if (!bBacklogAvailable)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[TerrainLoadGate] Counters unavailable; using fallback warmup. Elapsed=%.1f"),
            ElapsedSeconds);

        if (LocalMeshWarmupDelaySeconds <= KINDA_SMALL_NUMBER)
        {
            CompleteSelectedTileCameraHandoff();
            return;
        }

        World->GetTimerManager().SetTimer(
            CameraHandoffTimerHandle,
            this,
            &AWorldGenBootstrapActor::CompleteSelectedTileCameraHandoff,
            LocalMeshWarmupDelaySeconds,
            false);
        return;
    }

    // Diagnostic: counters are suspiciously zero on the very first poll before voxel tasks are queued.
    if (ElapsedSeconds < 1.0f && Backlog.TotalTasksRemaining == 0 && Backlog.MeshTasksRemaining == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[TerrainLoadGate] Early zero counters detected; waiting for task queue stabilization."));
    }

    // Gate on all three counters.
    // TotalTasksRemaining and MeshActionsQueued may be -1 (unavailable); treat -1 as "not blocking".
    const bool bMeshDone    = Backlog.MeshTasksRemaining <= 0;
    const bool bTotalDone   = (Backlog.TotalTasksRemaining < 0) || (Backlog.TotalTasksRemaining <= 128);
    const bool bActionsDone = (Backlog.MeshActionsQueued  < 0) || (Backlog.MeshActionsQueued  <= 0);
    const bool bAllReady    = bMeshDone && bTotalDone && bActionsDone;

    // Reset streak if the backlog grew after a previous ready poll.
    if (ConsecutiveReadyPolls > 0)
    {
        const bool bTotalIncreased = (LastPollTotalTasks >= 0) && (Backlog.TotalTasksRemaining > LastPollTotalTasks);
        const bool bMeshIncreased  = (LastPollMeshTasks  >= 0) && (Backlog.MeshTasksRemaining  > LastPollMeshTasks);
        if (bTotalIncreased || bMeshIncreased)
        {
            UE_LOG(LogTemp, Log, TEXT("[TerrainLoadGate] Backlog increased after ready poll; resetting ready streak."));
            ConsecutiveReadyPolls = 0;
        }
    }

    if (bAllReady)
    {
        ++ConsecutiveReadyPolls;
    }
    else
    {
        ConsecutiveReadyPolls = 0;
    }

    LastPollTotalTasks = Backlog.TotalTasksRemaining;
    LastPollMeshTasks  = Backlog.MeshTasksRemaining;

    const bool bStableReady = bAllReady
        && (ElapsedSeconds >= MinTerrainLoadGateSeconds)
        && (ConsecutiveReadyPolls >= RequiredReadyPolls);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[TerrainLoadGate] Poll Tile=(%d,%d) Total=%d Mesh=%d Actions=%d Elapsed=%.1f Ready=%s StableReady=%d/%d"),
        PendingCameraHandoffTile.X,
        PendingCameraHandoffTile.Y,
        Backlog.TotalTasksRemaining,
        Backlog.MeshTasksRemaining,
        Backlog.MeshActionsQueued,
        ElapsedSeconds,
        bAllReady ? TEXT("true") : TEXT("false"),
        ConsecutiveReadyPolls,
        RequiredReadyPolls);

    if (bStableReady)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[TerrainLoadGate] Stable ready. Releasing play view. Total=%d Mesh=%d Actions=%d Elapsed=%.1f"),
            Backlog.TotalTasksRemaining,
            Backlog.MeshTasksRemaining,
            Backlog.MeshActionsQueued,
            ElapsedSeconds);
        CompleteSelectedTileCameraHandoff();
        return;
    }

    if (ElapsedSeconds >= VoxelMeshReadyMaxWaitSeconds)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[TerrainLoadGate] WARNING: Timeout with backlog still high. Total=%d Mesh=%d Actions=%d Elapsed=%.1f"),
            Backlog.TotalTasksRemaining,
            Backlog.MeshTasksRemaining,
            Backlog.MeshActionsQueued,
            ElapsedSeconds);
        CompleteSelectedTileCameraHandoff();
        return;
    }

    World->GetTimerManager().SetTimer(
        CameraHandoffTimerHandle,
        this,
        &AWorldGenBootstrapActor::PollVoxelMeshReady,
        VoxelMeshReadyPollIntervalSeconds,
        false);
}

void AWorldGenBootstrapActor::CompleteSelectedTileCameraHandoff()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);

    if (!bPendingSelectedTileCameraHandoff)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TerrainMeshReady] WARNING: No pending selected tile; using fallback release."));
        ClearPendingSelectedTileCameraHandoff();
        ReleasePrePlayBlock();
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[CameraHandoff] Mesh gate complete. Snapping camera Tile=(%d,%d) World=(%.1f,%.1f,%.1f)"),
        PendingCameraHandoffTile.X,
        PendingCameraHandoffTile.Y,
        PendingCameraHandoffWorldLocation.X,
        PendingCameraHandoffWorldLocation.Y,
        PendingCameraHandoffWorldLocation.Z);

    APlayerController* PlayerController = World->GetFirstPlayerController();
    AWorldForgeCamera* CameraPawn = FindWorldForgeCamera(World, PlayerController);
    if (!CameraPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CameraHandoff] WARNING: No camera pawn found."));
        ClearPendingSelectedTileCameraHandoff();
        ReleasePrePlayBlock();
        return;
    }

    float CameraYaw = 0.0f;
    float CameraPitch = 0.0f;
    float CameraZoom = 0.0f;
    ResolveCameraHandoffView(CameraPawn, PlayerController, CameraYaw, CameraPitch, CameraZoom);

    if (PlayerController && PlayerController->GetPawn() != CameraPawn)
    {
        PlayerController->Possess(CameraPawn);
    }

    CameraPawn->SnapToRTSView(PendingCameraHandoffWorldLocation, CameraYaw, CameraPitch, CameraZoom, true);

    if (PlayerController)
    {
        PlayerController->SetViewTarget(CameraPawn);
        PlayerController->SetControlRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
    }

    if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
    {
        VoxelMgr->SetConfirmedEmbarkTile(PendingCameraHandoffTile.X, PendingCameraHandoffTile.Y);
        UE_LOG(LogTemp, Log,
            TEXT("[EmbarkFlow] Confirmed embark tile handed to VoxelWorldManager: (%d,%d)"),
            PendingCameraHandoffTile.X,
            PendingCameraHandoffTile.Y);
    }

    ClearPendingSelectedTileCameraHandoff();
    ReleasePrePlayBlock();
}

bool AWorldGenBootstrapActor::StartVoxelTerrainHandoff()
{
    UWorld* World = GetWorld();
    if (!World || !Pipeline || !Pipeline->IsComplete())
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkFlow] WARNING: Voxel terrain handoff skipped because bootstrap state is incomplete."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Starting voxel terrain handoff after map subsystem initialization."));

    if (!World->GetSubsystem<UVoxelWorldManager>())
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkFlow] WARNING: VoxelWorldManager missing; generated world data exists but playable terrain was not started."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Voxel terrain handoff requested."));
    const bool bAccepted = Pipeline->DispatchVoxelTerrainHandoff(this);
    if (!bAccepted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EmbarkFlow] WARNING: Voxel terrain handoff request was not accepted."));
    }

    return bAccepted;
}

void AWorldGenBootstrapActor::HandleTerrainReady()
{
    if (bDecorationPassDispatched)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || !Pipeline || !Pipeline->IsComplete())
    {
        return;
    }

    if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
    {
        if (!VM->IsSurfaceReady())
        {
            return;
        }
    }

    UWorldDecorationSubsystem* DecorationSubsystem = World->GetSubsystem<UWorldDecorationSubsystem>();
    if (!DecorationSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Decoration dispatch failed: WorldDecorationSubsystem missing."));
        return;
    }

    const bool bGenerated = DecorationSubsystem->GenerateDecorations(
        Pipeline->GetWorldTiles(),
        Pipeline->GetGridWidth(),
        Pipeline->GetGridHeight(),
        WorldSeed
    );

    bDecorationPassDispatched = true;
    bTerrainVisualReadyForPlay = true;
    UE_LOG(LogTemp, Log, TEXT("[TerrainMeshReady] TerrainReady received but play release is deferred for local mesh warm-up."));

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WorldGenBootstrap] Decoration triggered via terrain-ready event. Spawned=%s"),
        bGenerated ? TEXT("true") : TEXT("false")
    );

    SpawnWaterlineLakeSurfaces();
    SpawnWaterlineRiverSurfaces();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[TerrainGen] NOTE: Visual terrain gaps may persist until voxel mesh tasks complete."));

    if (bFullMapConfirmed)
    {
        ProcessPendingSelectedTileCameraHandoff();
    }

    if (bDestroyAfterBootstrap &&
        !bWorldForgePrePlayBlocking &&
        !bPendingSelectedTileCameraHandoff &&
        !World->GetTimerManager().IsTimerActive(CameraHandoffTimerHandle))
    {
        Destroy();
    }
}

void AWorldGenBootstrapActor::RunBootstrap()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] BeginPlay failed: GetWorld() returned null."));
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Failed: GameInstance is null."));
        return;
    }

    RNG = GameInstance->GetSubsystem<UWorldForgeRNG>();
    if (!RNG)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Failed to get UWorldForgeRNG subsystem from GameInstance."));
        return;
    }

    if (PipelineClass)
    {
        Pipeline = NewObject<UWorldGenPipeline>(this, PipelineClass);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGenBootstrap] PipelineClass not set. Falling back to base class."));
        Pipeline = NewObject<UWorldGenPipeline>(this);
    }

    // ------------------------------------------------------------
    // Frontend / Embarkation config bridge.
    //
    // If the player came from Frontend_Map, the seed + world size are stored in
    // UWorldForgeEmbarkationManager, which lives on the GameInstance and survives OpenLevel.
    //
    // If no user-driven embark config exists, we fall back to this actor's Details-panel defaults.
    // ------------------------------------------------------------
    if (UWorldForgeEmbarkationManager* EmbarkationManager =
        GameInstance->GetSubsystem<UWorldForgeEmbarkationManager>())
    {
        if (EmbarkationManager->HasUserEmbarkConfig())
        {
            const FString SeedString = EmbarkationManager->GetSeedString();
            const int32 EmbarkWorldSize = EmbarkationManager->GetWorldSize();

            WorldSeed = static_cast<int64>(EmbarkationManager->GetResolvedSeed());

            switch (EmbarkWorldSize)
            {
            case 128:
                WorldGenConfig.WorldSize = EWorldSize::Small;
                break;

            case 512:
                WorldGenConfig.WorldSize = EWorldSize::Large;
                break;

            case 1024:
                WorldGenConfig.WorldSize = EWorldSize::OMG;
                break;

            case 256:
            default:
                WorldGenConfig.WorldSize = EWorldSize::Medium;
                break;
            }

            UE_LOG(LogTemp, Log,
                TEXT("[WorldGenBootstrap] Using embarkation config. SeedString='%s' ResolvedSeed=%lld WorldSizeGrid=%d WorldSizeEnum=%d"),
                *SeedString,
                WorldSeed,
                EmbarkWorldSize,
                static_cast<int32>(WorldGenConfig.WorldSize));
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[WorldGenBootstrap] No user embark config found. Falling back to actor defaults. Seed=%lld WorldSizeEnum=%d"),
                WorldSeed,
                static_cast<int32>(WorldGenConfig.WorldSize));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldGenBootstrap] EmbarkationManager not found. Falling back to actor defaults. Seed=%lld WorldSizeEnum=%d"),
            WorldSeed,
            static_cast<int32>(WorldGenConfig.WorldSize));
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGenBootstrap] Starting world generation. Seed=%lld WorldSizeEnum=%d"),
        WorldSeed,
        static_cast<int32>(WorldGenConfig.WorldSize));

    RNG->InitializeWithSeed(WorldSeed);
    // CUBE-TERRAIN-01D-TEST: force the parallel cube prototype path so AWFCubeTerrainActor can build a 32x32 test patch.
    // Smooth terrain still falls through underneath in UVoxelWorldManager.
    WorldGenConfig.TerrainMode = ETerrainMode::CubePrototype;
    Pipeline->GenerateWorld(WorldSeed, WorldGenConfig, RNG);

    if (!Pipeline->IsComplete())
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Pipeline did not complete."));
        return;
    }

    if (UWorldMapSubsystem* WorldMap = World->GetSubsystem<UWorldMapSubsystem>())
    {
        WorldMap->InitializeFromPipeline(Pipeline);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] UWorldMapSubsystem not found."));
        return;
    }

    // Phase 1 active NPC civilization bridge. Worldgen creates civ data;
    // CivilizationActivitySubsystem turns that data into active settlements
    // with agents, stockpiles, farms, jobs, and VSS-visible building growth.
    if (UCivilizationActivitySubsystem* CivActivity = World->GetSubsystem<UCivilizationActivitySubsystem>())
    {
        CivActivity->InitializeFromGeneratedWorld(
            Pipeline->GetCivilizations(),
            Pipeline->GetWorldTiles(),
            Pipeline->GetGridWidth(),
            Pipeline->GetGridHeight(),
            true);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGenBootstrap] CivilizationActivitySubsystem not found; generated civs remain map-only."));
    }

    UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>();
    if (VM && !bBoundToTerrainReady)
    {
        VM->OnTerrainReady.AddDynamic(this, &AWorldGenBootstrapActor::HandleTerrainReady);
        bBoundToTerrainReady = true;
    }

    const bool bTerrainHandoffAccepted = StartVoxelTerrainHandoff();

    if (VM)
    {
        if (VM->IsSurfaceReady())
        {
            HandleTerrainReady();
        }
        else if (bTerrainHandoffAccepted)
        {
            UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] Waiting for terrain-ready event before decoration dispatch."));
        }
    }

    AlignWaterlineOceanToSeaLevel();

    if (FullMapWidgetClass)
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            UUserWidget* FullMapWidget = CreateWidget<UUserWidget>(PC, FullMapWidgetClass);

            if (FullMapWidget)
            {
                FullMapWidget->AddToViewport(50);

                PC->bShowMouseCursor = true;

                FInputModeUIOnly InputMode;
                InputMode.SetWidgetToFocus(FullMapWidget->TakeWidget());
                InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                PC->SetInputMode(InputMode);

                PendingFullMapWidgetInstance = FullMapWidget;
                bWorldForgePrePlayBlocking = true;
                SetLegacyHudSuppressedForFullMap(World, true);
                RequestNavigatorCompassHideForMapUI(World);
                UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] PrePlay blocking active."));
                UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] Full map widget created."));
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGenBootstrap] FullMapWidgetClass is not set."));
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] World generation and map initialization complete."));
}

int32 AWorldGenBootstrapActor::CleanupSpawnedLightweightLakeActors()
{
    int32 DestroyedCount = 0;

    for (TObjectPtr<AWorldForgeLakeSurfaceActor>& LakeActor : SpawnedLightweightLakeActors)
    {
        if (IsValid(LakeActor))
        {
            LakeActor->Destroy();
            ++DestroyedCount;
        }
    }

    SpawnedLightweightLakeActors.Reset();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeLight] Cleanup destroyed %d previous lightweight lake actors."),
        DestroyedCount);

    return DestroyedCount;
}

int32 AWorldGenBootstrapActor::CleanupSpawnedLightweightRiverActors()
{
    int32 DestroyedCount = 0;

    for (TObjectPtr<AWorldForgeRiverSurfaceActor>& RiverActor : SpawnedLightweightRiverActors)
    {
        if (IsValid(RiverActor))
        {
            RiverActor->Destroy();
            ++DestroyedCount;
        }
    }

    SpawnedLightweightRiverActors.Reset();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineRiverLight] Cleanup destroyed %d previous lightweight river actors."),
        DestroyedCount);

    return DestroyedCount;
}

void AWorldGenBootstrapActor::ApplyWaterlineLakePoolPlacement(const TArray<FWaterlineLakeSurfaceDesc>& Descs)
{
    if (bWaterlineLakePoolApplied)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UClass* LakeActorClass = LoadClass<AActor>(nullptr, WaterlineLakePoolClassPath);
    if (!LakeActorClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WaterlineLake] WARNING: BP_Lake_2 class not found. Path=%s"),
            WaterlineLakePoolClassPath);
        UE_LOG(LogTemp, Warning,
            TEXT("[WaterlineLake] WARNING: No preplaced BP_Lake_2 pool actors found. Add BP_Lake_2 actors to WorldGenTest or enable a future lightweight fallback."));
        return;
    }

    TArray<AActor*> LakePoolActors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* CandidateActor = *It;
        if (IsValid(CandidateActor) && CandidateActor->IsA(LakeActorClass))
        {
            LakePoolActors.Add(CandidateActor);
        }
    }

    if (LakePoolActors.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WaterlineLake] WARNING: No preplaced BP_Lake_2 pool actors found. Add BP_Lake_2 actors to WorldGenTest or enable a future lightweight fallback."));
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WaterlineLake] Pool found. Actors=%d Descs=%d"),
        LakePoolActors.Num(),
        Descs.Num());

    if (Descs.Num() == 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[WaterlineLake] No generated lake descriptors available; staging all BP_Lake_2 pool actors."));
    }

    const float SafeBaseHalfExtent = FMath::Max(1.0f, WaterlineLakeBaseHalfExtent);
    const float SafeMinScaleXY = FMath::Max(0.01f, MinWaterlineLakeScaleXY);
    const float SafeMaxScaleXY = FMath::Max(SafeMinScaleXY, MaxWaterlineLakeScaleXY);
    const float SafeMinHalfExtent = FMath::Max(0.0f, MinWaterlineLakeHalfExtent);

    const int32 PlacementCount = FMath::Min(Descs.Num(), LakePoolActors.Num());
    int32 PlacedCount = 0;
    int32 UnusedCount = 0;

    for (int32 Index = 0; Index < PlacementCount; ++Index)
    {
        const FWaterlineLakeSurfaceDesc& Desc = Descs[Index];
        AActor* LakeActor = LakePoolActors[Index];
        if (!IsValid(LakeActor))
        {
            continue;
        }

        const float EffectiveHalfExtentX = FMath::Max(Desc.HalfExtentX, SafeMinHalfExtent);
        const float EffectiveHalfExtentY = FMath::Max(Desc.HalfExtentY, SafeMinHalfExtent);
        const float UnclampedScaleX = EffectiveHalfExtentX / SafeBaseHalfExtent;
        const float UnclampedScaleY = EffectiveHalfExtentY / SafeBaseHalfExtent;
        const float ScaleX = FMath::Clamp(UnclampedScaleX, SafeMinScaleXY, SafeMaxScaleXY);
        const float ScaleY = FMath::Clamp(UnclampedScaleY, SafeMinScaleXY, SafeMaxScaleXY);

        const FVector LakeLocation(
            Desc.WorldCenter.X,
            Desc.WorldCenter.Y,
            Desc.WaterSurfaceZ + WaterlineLakeZOffset);

        LakeActor->SetActorLocation(LakeLocation);
        SetWaterlineLakeRuntimeFlags(LakeActor, true);

        const bool bCanSetScaleVariables =
            IsWaterlineNumericVariableSettable(LakeActor, TEXT("Scale X")) &&
            IsWaterlineNumericVariableSettable(LakeActor, TEXT("Scale Y"));

        if (bCanSetScaleVariables)
        {
            SetWaterlineNumericVariable(LakeActor, TEXT("Scale X"), ScaleX);
            SetWaterlineNumericVariable(LakeActor, TEXT("Scale Y"), ScaleY);
            LakeActor->SetActorScale3D(FVector::OneVector);
        }
        else
        {
            LakeActor->SetActorScale3D(FVector(ScaleX, ScaleY, 1.0f));
        }

        ++PlacedCount;

        UE_LOG(LogTemp, Log,
            TEXT("[WaterlineLake] PoolPlace Basin=%d Actor=%s Location=(%.0f,%.0f,%.0f) HalfExtent=(%.0f,%.0f) Tiles=%d"),
            Desc.BasinID,
            *LakeActor->GetName(),
            LakeLocation.X,
            LakeLocation.Y,
            LakeLocation.Z,
            EffectiveHalfExtentX,
            EffectiveHalfExtentY,
            Desc.TileCount);
    }

    for (int32 Index = PlacementCount; Index < LakePoolActors.Num(); ++Index)
    {
        AActor* LakeActor = LakePoolActors[Index];
        if (!IsValid(LakeActor))
        {
            continue;
        }

        FVector StagingLocation = LakeActor->GetActorLocation();
        StagingLocation.Z = WaterlineLakeUnusedStagingZ;
        LakeActor->SetActorLocation(StagingLocation);
        SetWaterlineLakeRuntimeFlags(LakeActor, false);
        ++UnusedCount;

        UE_LOG(LogTemp, Log,
            TEXT("[WaterlineLake] Pool unused Actor=%s moved to staging."),
            *LakeActor->GetName());
    }

    const int32 MissingCount = FMath::Max(0, Descs.Num() - LakePoolActors.Num());
    bWaterlineLakePoolApplied = true;

    UE_LOG(LogTemp, Log,
        TEXT("[WaterlineLake] Pool complete. Placed=%d Unused=%d Missing=%d Descs=%d"),
        PlacedCount,
        UnusedCount,
        MissingCount,
        Descs.Num());
}

void AWorldGenBootstrapActor::SpawnLightweightWaterlineLakeSurfaces(const TArray<FWaterlineLakeSurfaceDesc>& Descs)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WaterlineLakeLight] Descs=%d"), Descs.Num());
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeLight] Tuning Coverage=%.2f MinHalfExtent=%.0f MinScaleXY=%.2f MaxScaleXY=%.2f ZOffset=%.1f"),
        LightweightLakeCoverageScale,
        MinLightweightLakeHalfExtent,
        MinLightweightLakeScaleXY,
        MaxLightweightLakeScaleXY,
        LightweightLakeZOffset);

    if (Descs.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[WaterlineLakeLight] No lake descriptors available."));
        UE_LOG(LogTemp, Log, TEXT("[WaterlineLakeLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"), 0, 0, 0, 0);
        return;
    }

    UStaticMesh* LakeMesh = LoadObject<UStaticMesh>(nullptr, WaterlineLakeLightMeshPath);
    if (!LakeMesh)
    {
        LakeMesh = LoadObject<UStaticMesh>(nullptr, WaterlineLakeLightFallbackMeshPath);
    }

    UMaterialInterface* LakeMaterial = LoadObject<UMaterialInterface>(nullptr, WaterlineLakeLightMaterialPath);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeLight] Loaded Mesh=%s Material=%s"),
        *GetPathNameSafe(LakeMesh),
        *GetPathNameSafe(LakeMaterial));

    if (LakeMesh)
    {
        const FBoxSphereBounds MeshBounds = LakeMesh->GetBounds();
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[WaterlineLakeLight] MeshBounds Mesh=%s BoxExtent=(%.1f,%.1f,%.1f) SphereRadius=%.1f"),
            *GetPathNameSafe(LakeMesh),
            MeshBounds.BoxExtent.X,
            MeshBounds.BoxExtent.Y,
            MeshBounds.BoxExtent.Z,
            MeshBounds.SphereRadius);
    }

    if (!LakeMesh || !LakeMaterial)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WaterlineLakeLight] WARNING: Lake mesh/material missing. MeshOK=%s MaterialOK=%s"),
            LakeMesh ? TEXT("true") : TEXT("false"),
            LakeMaterial ? TEXT("true") : TEXT("false"));
    }

    if (!LakeMesh)
    {
        UE_LOG(LogTemp, Log, TEXT("[WaterlineLakeLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"), 0, Descs.Num(), 0, Descs.Num());
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    int32 SpawnedCount = 0;
    int32 FailedCount = 0;
    int32 SkippedCount = 0;

    for (const FWaterlineLakeSurfaceDesc& Desc : Descs)
    {
        if (const TCHAR* const SkipReason = ValidateLightweightLakeDesc(Desc))
        {
            ++SkippedCount;
            LogLightweightLakeSkip(Desc, SkipReason);
            continue;
        }

        AWorldForgeLakeSurfaceActor* LakeActor = World->SpawnActor<AWorldForgeLakeSurfaceActor>(
            AWorldForgeLakeSurfaceActor::StaticClass(),
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            SpawnParams);

        if (!LakeActor)
        {
            ++FailedCount;
            continue;
        }

        LakeActor->InitializeLakeSurface(
            Desc,
            LakeMesh,
            LakeMaterial,
            LightweightLakeBaseHalfExtent,
            LightweightLakeCoverageScale,
            MinLightweightLakeHalfExtent,
            MinLightweightLakeScaleXY,
            MaxLightweightLakeScaleXY,
            LightweightLakeZOffset);

        SpawnedLightweightLakeActors.Add(LakeActor);
        ++SpawnedCount;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineLakeLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"),
        SpawnedCount,
        FailedCount,
        SkippedCount,
        Descs.Num());
}

void AWorldGenBootstrapActor::SpawnLightweightRiverSurfaces(const TArray<FWorldForgeRiverSurfaceDesc>& Descs)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>();
    if (!VM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RiverFix] WARNING: VoxelWorldManager missing during lightweight river spawn."));
        return;
    }

    const float TileSizeWorld = VM->GetTerrainCellSize();
    if (TileSizeWorld <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RiverFix] WARNING: Invalid terrain cell size for river surface actor spawn."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WaterlineRiverLight] Descs=%d"), Descs.Num());
    if (Descs.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[WaterlineRiverLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"), 0, 0, 0, 0);
        return;
    }

    UStaticMesh* RiverMesh = LoadObject<UStaticMesh>(nullptr, WaterlineRiverLightMeshPath);
    if (!RiverMesh)
    {
        RiverMesh = LoadObject<UStaticMesh>(nullptr, WaterlineRiverLightFallbackMeshPath);
    }

    UMaterialInterface* RiverMaterial = LoadObject<UMaterialInterface>(nullptr, WaterlineRiverLightMaterialPath);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineRiverLight] Loaded Mesh=%s Material=%s"),
        *GetPathNameSafe(RiverMesh),
        *GetPathNameSafe(RiverMaterial));

    if (!RiverMesh || !RiverMaterial)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WaterlineRiverLight] WARNING: River mesh/material missing. MeshOK=%s MaterialOK=%s"),
            RiverMesh ? TEXT("true") : TEXT("false"),
            RiverMaterial ? TEXT("true") : TEXT("false"));
    }

    if (!RiverMesh)
    {
        UE_LOG(LogTemp, Log, TEXT("[WaterlineRiverLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"), 0, Descs.Num(), 0, Descs.Num());
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    int32 SpawnedCount = 0;
    int32 FailedCount = 0;
    int32 SkippedCount = 0;

    for (const FWorldForgeRiverSurfaceDesc& Desc : Descs)
    {
        if (const TCHAR* const SkipReason = ValidateLightweightRiverDesc(Desc))
        {
            ++SkippedCount;
            LogLightweightRiverSkip(Desc, SkipReason);
            continue;
        }

        AWorldForgeRiverSurfaceActor* RiverActor = World->SpawnActor<AWorldForgeRiverSurfaceActor>(
            AWorldForgeRiverSurfaceActor::StaticClass(),
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            SpawnParams);

        if (!RiverActor)
        {
            ++FailedCount;
            continue;
        }

        RiverActor->InitializeRiverSurface(
            Desc,
            RiverMesh,
            RiverMaterial,
            TileSizeWorld,
            LightweightRiverZOffset);

        SpawnedLightweightRiverActors.Add(RiverActor);
        ++SpawnedCount;

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[RiverFix] river surface actor spawn River=%d Tiles=%d Width=%.2f"),
            Desc.RiverID,
            Desc.TileCount,
            Desc.AverageWidth);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WaterlineRiverLight] Complete. Spawned=%d Failed=%d Skipped=%d Descs=%d"),
        SpawnedCount,
        FailedCount,
        SkippedCount,
        Descs.Num());
}

void AWorldGenBootstrapActor::SpawnWaterlineRiverSurfaces()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>();
    if (!VM)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[RiverFix] WARNING: VoxelWorldManager not available for river surface placement."));
        return;
    }

    CleanupSpawnedLightweightRiverActors();

    if (!bUseLightweightRiverSurfaces)
    {
        UE_LOG(LogTemp, Log, TEXT("[RiverFix] Lightweight river surfaces disabled."));
        return;
    }

    const TArray<FWorldForgeRiverSurfaceDesc> Descs = VM->GetRiverSurfaceDescs();
    SpawnLightweightRiverSurfaces(Descs);
}

void AWorldGenBootstrapActor::SpawnWaterlineLakeSurfaces()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>();
    if (!VM)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WaterlineLake] WARNING: VoxelWorldManager not available for lake surface placement."));
        return;
    }

    CleanupSpawnedLightweightLakeActors();

    const TArray<FWaterlineLakeSurfaceDesc> Descs = VM->GetWaterlineLakeSurfaceDescs();

    if (bEnableWaterlineLakePoolPlacement)
    {
        ApplyWaterlineLakePoolPlacement(Descs);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WaterlineLake] BP_Lake_2 pool placement disabled; using lightweight lake surfaces."));
    SpawnLightweightWaterlineLakeSurfaces(Descs);
}

bool AWorldGenBootstrapActor::NotifyFullMapConfirmedAndDeferDismiss(UUserWidget* Widget, int32 TileX, int32 TileY)
{
    // CUBE-TERRAIN-03A: Set confirmed embark tile BEFORE cube terrain build.
    // This ensures BuildPatch() uses the correct center tile on first generation,
    // rather than falling back to GridCenter.
    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            VoxelMgr->SetConfirmedEmbarkTile(TileX, TileY);
            UE_LOG(LogTemp, Log,
                TEXT("[EmbarkFlow] Confirmed embark tile set before cube terrain build: (%d,%d)"),
                TileX,
                TileY);
        }
    }

    PendingFullMapWidgetInstance = Widget;
    QueueSelectedTileCameraHandoff(TileX, TileY);
    return true;
}

bool AWorldGenBootstrapActor::NotifyFullMapConfirmedAndDeferDismissWithSpawn(UUserWidget* Widget, int32 TileX, int32 TileY, const FVector& SpawnWorldLocation)
{
    // CUBE-TERRAIN-04H: Forward the FullMap-resolved spawn location so the
    // camera handoff does not need to re-derive it from the voxel surface.
    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            VoxelMgr->SetConfirmedEmbarkTile(TileX, TileY);
            UE_LOG(LogTemp, Log,
                TEXT("[EmbarkFlow] Confirmed embark tile set before cube terrain build: (%d,%d)"),
                TileX,
                TileY);
        }
    }

    PendingFullMapWidgetInstance = Widget;
    QueueSelectedTileCameraHandoff(TileX, TileY, SpawnWorldLocation);
    return true;
}

void AWorldGenBootstrapActor::NotifyFullMapCancelled()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(CameraHandoffTimerHandle);
    }

    bFullMapConfirmed = false;
    ClearPendingSelectedTileCameraHandoff();
    bWorldForgePrePlayBlocking = false;

    if (PendingFullMapWidgetInstance && PendingFullMapWidgetInstance->IsInViewport())
    {
        PendingFullMapWidgetInstance->RemoveFromParent();
    }

    PendingFullMapWidgetInstance = nullptr;
    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Full map closed without confirm; releasing pre-play block."));

    if (UWorld* World = GetWorld())
    {
        SetLegacyHudSuppressedForFullMap(World, false);
        RequestNavigatorCompassShowForGameplay(World);

        if (AWorldForgePlayerController* WFPC = Cast<AWorldForgePlayerController>(World->GetFirstPlayerController()))
        {
            WFPC->NotifyCameraHandoffComplete();
        }
    }
}

void AWorldGenBootstrapActor::AlignWaterlineOceanToSeaLevel()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>();
    if (!VM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[OceanZSync] WARNING: VoxelWorldManager not available; ocean Z alignment skipped."));
        return;
    }

    const float SeaLevelWorldZ = VM->GetGeneratedSeaLevelWorldZ();
    UE_LOG(LogTemp, Log, TEXT("[OceanZSync] GeneratedSeaLevelWorldZ=%.1f"), SeaLevelWorldZ);

    int32 AdjustedCount = 0;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        const FString ActorName = Actor->GetName();
        const FString ClassName = Actor->GetClass() ? Actor->GetClass()->GetName() : FString();

        if (!ActorName.Contains(TEXT("Waterline_Ocean")) && !ClassName.Contains(TEXT("Waterline_Ocean")))
        {
            continue;
        }

        const FVector OldLocation = Actor->GetActorLocation();
        const float OldZ = OldLocation.Z;
        const float Delta = SeaLevelWorldZ - OldZ;

        FVector NewLocation = OldLocation;
        NewLocation.Z = SeaLevelWorldZ;
        Actor->SetActorLocation(NewLocation);

        UE_LOG(LogTemp, Log,
            TEXT("[OceanZSync] Adjusted Actor=%s OldZ=%.1f NewZ=%.1f Delta=%.1f"),
            *ActorName,
            OldZ,
            SeaLevelWorldZ,
            Delta);

        if (FMath::Abs(Delta) > 500.0f)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[OceanZSync] WARNING: Large ocean Z delta detected. Actor=%s Delta=%.1f — check sea level formula or ocean actor placement."),
                *ActorName,
                Delta);
        }

        ++AdjustedCount;
    }

    if (AdjustedCount == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanZSync] WARNING: No Waterline_Ocean actors found in level. Ocean Z alignment skipped."));
    }
    else
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OceanZSync] Complete. Adjusted=%d actors to SeaLevelWorldZ=%.1f"),
            AdjustedCount,
            SeaLevelWorldZ);
    }
}

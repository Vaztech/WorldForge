#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "ColonyManager.generated.h"

class USettlementManager;
class UProcessorManager;
class UNavigationContext;
class USimulationTimeManager;
class USaveSubsystem;
class UVoxelConstructionSystem;
class UVoxelWorldManager;
class AWorldGenBootstrapActor;

UCLASS()
class WORLDFORGE_API AColonyManager : public AActor
{
    GENERATED_BODY()

public:
    AColonyManager();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Colony")
    void BeginBootstrapNow();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool IsPrimaryColonyManagerInstance() const;

    void StartBootstrapAttempt();
    void RetryBootstrap();
    bool CacheSubsystems();
    bool AreRequiredSubsystemsReady() const;
    bool IsTerrainReadyForSpawn() const;
    void BindToTerrainReadyEvent();

    UFUNCTION()
    void HandleTerrainReady();

    bool HasWorldGenBootstrapActor() const;

    void FinishBootstrap();
    void InitializeSettlement();
    void InitializeNavigation();
    void SetupAgents();
    void SetupInitialResources();
    void StartSimulation();

    void BindDebugInput();
    FIntPoint ResolveInitialSettlementWorldOrigin() const;
    FIntPoint ComputeSettlementOriginFromSpawnCenter(const FIntPoint& InSpawnCenter) const;
    void PositionPlayerCameraOverSettlement() const;

    FIntPoint ComputeInitialSpawnCell(int32 AgentIndex) const;
    FIntPoint ResolveValidSpawnCell(const FIntPoint& DesiredCell) const;
    FIntPoint ResolveUniqueValidSpawnCell(const FIntPoint& DesiredCell, const TSet<FIntPoint>& ClaimedWorldCells) const;
    bool IsWorldCellAvailableForSpawn(const FIntPoint& WorldCell, const TSet<FIntPoint>& ClaimedWorldCells) const;
    FIntPoint WorldCellToSettlementLocalCell(const FIntPoint& WorldCell) const;
    int32 GetNextAgentID() const;

    void HandleSaveInput();
    void HandleLoadInput();
    void HandlePlaceBuildingInput();
    void HandleBuildTaskInput();

private:
    UPROPERTY(Transient)
    TObjectPtr<USettlementManager> SettlementMgr = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UProcessorManager> ProcessorMgr = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UNavigationContext> NavContext = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USimulationTimeManager> SimTimeMgr = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USaveSubsystem> SaveSubsystem = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UVoxelConstructionSystem> VoxelConstructionSys = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UVoxelWorldManager> VoxelWorldMgr = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Colony")
    int32 SettlementID = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Colony|Bootstrap")
    bool bBootstrapOnBeginPlay = false;

    UPROPERTY(EditAnywhere, Category = "Colony|Bootstrap")
    float InitialBootstrapDelaySeconds = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Colony|Bootstrap")
    float SubsystemRetryDelaySeconds = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Colony|Bootstrap")
    int32 MaxBootstrapRetries = 40;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    FString SettlementName = TEXT("PlayerColony");

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 SettlementWorldTileID = 0;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 NavigationGridWidth = 256;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 NavigationGridHeight = 256;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 InitialAgentCount = 16;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 SpawnGridColumns = 4;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 SpawnSpacingCells = 1;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    bool bAnchorSettlementToPlayerViewOnStart = false;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    bool bPreferMapCenterOnStart = false;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup", meta = (ClampMin = "1", UIMin = "1"))
    int32 CameraAnchorSearchRadius = 24;

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    FIntPoint SettlementWorldOrigin = FIntPoint(128, 128);

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    FIntPoint SpawnGridCenter = FIntPoint(128, 128);

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 SpawnSearchRadius = 16;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera")
    bool bPositionRTSCameraOnBootstrap = true;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera")
    float RTSCameraHeight = 20000.0f;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera")
    float RTSCameraPitch = -60.0f;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera")
    float RTSCameraYaw = 45.0f;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera")
    bool bUseManualCameraStart = false;

    UPROPERTY(EditAnywhere, Category = "Colony|Camera", meta = (EditCondition = "bUseManualCameraStart"))
    FVector ManualCameraLocation = FVector(126.0f, 126.0f, 0.0f);

    UPROPERTY(EditAnywhere, Category = "Colony|Debug")
    bool bEnableDebugKeyInput = true;

    UPROPERTY(EditAnywhere, Category = "Colony|Debug")
    FString SaveSlotName = TEXT("TestSlot");

    bool bBootstrapComplete = false;
    bool bBoundToTerrainReadyEvent = false;
    bool bIsDuplicateManagerInstance = false;
    int32 BootstrapRetryCount = 0;

    FTimerHandle BootstrapTimerHandle;
};
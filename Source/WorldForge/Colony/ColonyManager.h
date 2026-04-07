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

UCLASS()
class WORLDFORGE_API AColonyManager : public AActor
{
    GENERATED_BODY()

public:
    AColonyManager();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void StartBootstrapAttempt();
    void RetryBootstrap();
    bool CacheSubsystems();
    bool AreRequiredSubsystemsReady() const;
    bool IsTerrainReadyForSpawn() const;
    void BindToTerrainReadyEvent();

    UFUNCTION()
    void HandleTerrainReady();

    void FinishBootstrap();
    void InitializeSettlement();
    void InitializeNavigation();
    void SetupAgents();
    void SetupInitialResources();
    void StartSimulation();

    void BindDebugInput();
    FIntPoint ResolveInitialSettlementWorldOrigin() const;
    FIntPoint ComputeInitialSpawnCell(int32 AgentIndex) const;
    FIntPoint ResolveValidSpawnCell(const FIntPoint& DesiredCell) const;
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
    bool bBootstrapOnBeginPlay = true;

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

    // If enabled, the settlement origin is resolved near the player's initial
    // camera/viewpoint so the colony starts close to what the player sees.
    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    bool bAnchorSettlementToPlayerViewOnStart = true;

    // Additional search radius used when snapping the camera-derived start cell
    // to the nearest valid spawnable terrain cell.
    UPROPERTY(EditAnywhere, Category = "Colony|Setup", meta = (ClampMin = "1", UIMin = "1"))
    int32 CameraAnchorSearchRadius = 24;

    // This is the WORLD terrain-cell anchor where the settlement lives.
    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    FIntPoint SettlementWorldOrigin = FIntPoint(128, 128);

    // Spawn pattern around the settlement origin, expressed in WORLD cells before
    // conversion into local settlement/nav cells.
    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    FIntPoint SpawnGridCenter = FIntPoint(128, 128);

    UPROPERTY(EditAnywhere, Category = "Colony|Setup")
    int32 SpawnSearchRadius = 16;

    UPROPERTY(EditAnywhere, Category = "Colony|Debug")
    bool bEnableDebugKeyInput = true;

    UPROPERTY(EditAnywhere, Category = "Colony|Debug")
    FString SaveSlotName = TEXT("TestSlot");

    bool bBootstrapComplete = false;
    bool bBoundToTerrainReadyEvent = false;
    int32 BootstrapRetryCount = 0;

    FTimerHandle BootstrapTimerHandle;
};
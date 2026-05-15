#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/TimerHandle.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldForgeDataTypes.h"
#include "ColonistVisualManager.generated.h"

class AWorldForgeColonist;
class UProcessorManager;
class USimulationTimeManager;
class UVoxelWorldManager;
class UDataTable;


/**
 * Editor-friendly registry asset for colonist visual DataTables.
 *
 * Create one Data Asset of this class, usually saved as:
 *   /Game/WorldForge/Data/Colonists/DA_ColonistVisualRegistry
 *
 * Then add entries such as:
 *   WorldForge.Race.Dwarf   -> DT_ColonistVisual_Dwarf
 *   WorldForge.Race.HighElf -> DT_ColonistVisual_HighElf
 */
UCLASS(BlueprintType)
class WORLDFORGE_API UColonistVisualRegistryAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Registry")
    TArray<FColonistRaceVisualTable> RaceVisualTables;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Registry")
    FName DefaultVisualRowName = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Fallback")
    FWorldForgeColonistVisualRow NativeFallbackVisual;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UColonistVisualManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SpawnColonistActors(int32 InSettlementID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SyncActorsToAgents(int32 InSettlementID);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual")
    AWorldForgeColonist* GetActorForAgent(int32 AgentID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void DestroyAllColonistActors();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    void SetRaceVisualTable(FGameplayTag RaceTag, UDataTable* VisualTable);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual|Registry")
    const TArray<FColonistRaceVisualTable>& GetRegisteredRaceVisualTables() const { return RaceVisualTables; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual|Registry")
    UDataTable* GetRaceVisualTable(FGameplayTag RaceTag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    TArray<FName> GetVisualRowNamesForRace(FGameplayTag RaceTag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    bool GetVisualDefinitionForRace(FGameplayTag RaceTag, FName RowName, FWorldForgeColonistVisualRow& OutDefinition) const;

    /** Loads all race visual tables from a registry data asset. Safe to call from UI/Blueprint after edits. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    bool LoadVisualRegistryAsset(UColonistVisualRegistryAsset* RegistryAsset);

    /** Attempts to load the registry asset from DefaultVisualRegistryPath. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    bool LoadDefaultVisualRegistryAsset();

    /** Synchronously preloads every visual asset referenced by all registered race tables. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual|Registry")
    int32 PreloadAllRaceVisualAssets();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual")
    TSubclassOf<AWorldForgeColonist> ColonistActorClass;

    /** Optional explicit registry reference. Usually auto-loaded from DefaultVisualRegistryPath. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual|Registry")
    TObjectPtr<UColonistVisualRegistryAsset> VisualRegistryAsset = nullptr;

    /** Default auto-load path. Create the asset at this path and the manager will find it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual|Registry")
    FSoftObjectPath DefaultVisualRegistryPath = FSoftObjectPath(TEXT("/Game/WorldForge/Data/Colonists/DA_ColonistVisualRegistry.DA_ColonistVisualRegistry"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual|Registry")
    TArray<FColonistRaceVisualTable> RaceVisualTables;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual|Registry")
    FName DefaultVisualRowName = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual|Registry")
    FWorldForgeColonistVisualRow NativeFallbackVisual;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual")
    float InitialSpawnDelaySeconds = 0.20f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Visual|Registry")
    int32 PreloadedVisualAssetCount = 0;

private:
    UFUNCTION()
    void HandleSimulationStep(int32 StepNumber);

    UFUNCTION()
    void HandleTerrainReady();

    void InternalSpawnColonistActors(int32 InSettlementID);

    AWorldForgeColonist* SpawnActorFromData(const struct FAgentData& AgentData, float CellSize);
    bool ResolveVisualDefinitionForAgent(const struct FAgentData& AgentData, FWorldForgeColonistVisualRow& OutDefinition) const;
    void DestroyActorForAgent(int32 AgentID);

    void PreloadVisualRowAssets(const FWorldForgeColonistVisualRow& Row, const FString& Context);
    void PreloadSoftVisualAsset(const TSoftObjectPtr<UObject>& Asset, const FString& Context);

    float ResolveCellSize() const;

private:
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<AWorldForgeColonist>> ColonistActors;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> PreloadedVisualAssets;

    UPROPERTY(Transient)
    TSet<int32> ActiveSettlementIDs;

    UPROPERTY(Transient)
    TSet<int32> PendingSpawnSettlementIDs;

    UPROPERTY(Transient)
    TObjectPtr<UProcessorManager> CachedProcessorManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USimulationTimeManager> CachedSimulationTimeManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UVoxelWorldManager> CachedVoxelWorldManager = nullptr;

    FTimerHandle DeferredSpawnTimerHandle;
    bool bSpawnDeferredScheduled = false;
    bool bIsSyncInProgress = false;
    bool bBoundToTerrainReady = false;
};

UCLASS()
class WORLDFORGE_API UWorldForgeColonistVisualBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Convenience accessor so UI Blueprints can find the visual manager from any world context. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual|Registry", meta = (WorldContext = "WorldContextObject"))
    static UColonistVisualManager* GetColonistVisualManager(const UObject* WorldContextObject);
};

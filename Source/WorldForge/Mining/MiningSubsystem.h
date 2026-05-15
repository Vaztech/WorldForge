#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Mining/MiningTypes.h"
#include "MiningSubsystem.generated.h"

class UVoxelWorldManager;

/**
 * Manages mining designations for colonist AI.
 *
 * v5.0: CompleteDesignation now uses VoxelWorldManager::ApplyRuntimeBrush()
 * with a sphere SDF brush instead of RemoveBlockAt(). The brush ID is stored
 * on the designation for potential undo/rollback.
 *
 * v5.1 (Stability): CompleteDesignation calls UStabilitySubsystem::OnVoxelRemoved()
 * immediately after a successful dig. Any cascade cave-in voxels are cancelled
 * via CancelDesignationsAtVoxel(). TryReserveDesignation skips underground
 * designations that have no air-column path to the surface yet.
 */
UCLASS()
class WORLDFORGE_API UMiningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable)
	bool CreateMiningDesignation(
		int32 SettlementID,
		FIntVector GridPosition,
		int32 CurrentStep,
		int32& OutDesignationID);

	UFUNCTION(BlueprintCallable)
	bool CancelMiningDesignation(int32 DesignationID);

	UFUNCTION(BlueprintCallable)
	bool TryReserveDesignation(
		int32 SettlementID,
		int32 AgentID,
		int32& OutDesignationID,
		FIntVector& OutGridPosition);

	UFUNCTION(BlueprintCallable)
	bool CompleteDesignation(int32 DesignationID, int32 AgentID);

	UFUNCTION(BlueprintCallable)
	bool FailDesignation(int32 DesignationID, int32 AgentID);

	UFUNCTION(BlueprintCallable)
	void GetPendingDesignations(
		int32 SettlementID,
		TArray<FMiningDesignation>& OutDesignations) const;

	UFUNCTION(BlueprintCallable)
	void GetAllDesignations(
		int32 SettlementID,
		TArray<FMiningDesignation>& OutDesignations) const;

	/**
	 * Posts queued colonist jobs for pending mining designations.
	 *
	 * TargetID      = Mining DesignationID
	 * TargetGridPos = Designation grid X/Y
	 *
	 * Called from JobProcessor when it is generating available work.
	 */
	void PostMiningJobs(int32 SettlementID, int32 StepNumber);

	/**
	 * Query the brush shape that will be used for a given designation.
	 * Allows AI planners to understand the excavation geometry.
	 */
	UFUNCTION(BlueprintPure)
	EDigBrushShape GetDesignationBrushShape(int32 DesignationID) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Mining")
	void SetMiningRemovalMode(EMiningRemovalMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Mining")
	EMiningRemovalMode ToggleMiningRemovalMode();

	UFUNCTION(BlueprintPure, Category = "WorldForge|Mining")
	EMiningRemovalMode GetMiningRemovalMode() const { return CurrentRemovalMode; }

	UFUNCTION(BlueprintPure, Category = "WorldForge|Mining")
	FText GetMiningRemovalModeText() const;

	/**
	 * Cancels all non-terminal designations whose VoxelGrid matches the
	 * given collapsed voxel. Called by CompleteDesignation after a cave-in
	 * so agents are not sent into destroyed terrain.
	 */
	void CancelDesignationsAtVoxel(FIntVector VoxelGrid, int32 SettlementID);

private:
	UPROPERTY()
	TObjectPtr<UVoxelWorldManager> VoxelWorldManager = nullptr;

	UPROPERTY()
	TMap<int32, FMiningDesignation> Designations;

	int32 NextDesignationID = 1;

	UPROPERTY(Transient)
	EMiningRemovalMode CurrentRemovalMode = EMiningRemovalMode::VoxelCube;

private:
	bool IsDesignationTerminal(const FMiningDesignation& Designation) const;

	UVoxelWorldManager* GetVoxelWorldManager();

	bool ResolveMineTarget(
		FIntVector GridPosition,
		FIntVector& OutResolvedGridPosition,
		FGameplayTag& OutBlockType);

	bool IsValidMineTarget(
		FIntVector GridPosition,
		FGameplayTag& OutBlockType);
};

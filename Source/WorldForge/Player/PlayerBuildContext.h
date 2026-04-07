#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "PlayerBuildContext.generated.h"

// UPlayerBuildContext
// Minimal player-side build state container.
// Tracks selection + material and commits to UVoxelConstructionSystem.
// Does NOT modify simulation directly except via construction system entry points.

UCLASS(BlueprintType)
class WORLDFORGE_API UPlayerBuildContext : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

    // ================================
    // Selection Management
    // ================================

    // Adds a grid cell to the current selection.
    UFUNCTION(BlueprintCallable)
    void AddCell(FIntPoint Cell);

    // Clears current selection.
    UFUNCTION(BlueprintCallable)
    void ClearSelection();

    // ================================
    // Context Configuration
    // ================================

    // Sets the material used for the next build commit.
    UFUNCTION(BlueprintCallable)
    void SetMaterial(FGameplayTag MaterialTag);

    // Sets which settlement this build belongs to.
    UFUNCTION(BlueprintCallable)
    void SetActiveSettlement(int32 SettlementID);

    // ================================
    // Commit
    // ================================

    // Legacy custom-build entry point kept for compatibility.
    UFUNCTION(BlueprintCallable)
    bool CommitBuild(int32 AgentID);

    UFUNCTION(BlueprintCallable)
    bool CommitCustomBuild(int32 AgentID);

    // Legacy prefab path kept for compatibility with older callers.
    UFUNCTION(BlueprintCallable)
    bool CommitPrefabBuild(
        EBuildingPurpose Purpose,
        int32 EraIndex,
        FIntPoint Origin,
        int32 AgentID
    );

    // Legacy-compatible prefab path (rotation = 0).
    UFUNCTION(BlueprintCallable)
    bool CommitPrefabBuildByTag(
        FGameplayTag BuildingTag,
        FIntPoint Origin,
        int32 AgentID
    );

    // New preferred prefab path: commits explicit 3D blocks with rotation.
    UFUNCTION(BlueprintCallable)
    bool CommitPrefabBuildByTagWithRotation(
        FGameplayTag BuildingTag,
        FIntPoint Origin,
        int32 RotationIndex,
        int32 AgentID
    );

    UFUNCTION(BlueprintPure)
    FString GetLastCommitResultMessage() const { return LastCommitResultMessage; }

private:

    void SetLastCommitResultMessage(const FString& Message);

    bool TryParseExplicitPrefabDimensions(
        const FString& Source,
        int32& OutWidth,
        int32& OutDepth,
        int32& OutHeight
    ) const;

    FIntPoint RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const;

    bool BuildExplicitPrefabBlockList(
        FGameplayTag BuildingTag,
        FIntPoint Origin,
        int32 RotationIndex,
        TArray<FIntVector>& OutBlocks,
        FGameplayTag& OutMaterialTag
    ) const;

    int32 GetLegacyPrefabBlockCount(FGameplayTag BuildingTag) const;

    // Selected grid cells for the current build.
    UPROPERTY()
    TArray<FIntPoint> SelectedCells;

    // Selected material for build.
    UPROPERTY()
    FGameplayTag SelectedMaterial;

    // Target settlement.
    UPROPERTY()
    int32 ActiveSettlementID = -1;

    // Last user-facing result for UI feedback.
    UPROPERTY(Transient)
    FString LastCommitResultMessage;
};
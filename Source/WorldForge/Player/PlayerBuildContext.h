#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "Player/DigResult.h"
#include "Player/BuildModeTypes.h"
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

    // Canonical 3D custom-build commit. Cells3D is the explicit voxel list
    // (X/Y/Z) the player drew. Forwards to UVoxelConstructionSystem::
    // StartCustomBuildTask3D so stacking on top of placed cubes works.
    UFUNCTION(BlueprintCallable)
    bool CommitCustomBuild3D(const TArray<FIntVector>& Cells3D, int32 AgentID);

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

    // Legacy-compatible rotated prefab path at base Z = 0.
    UFUNCTION(BlueprintCallable)
    bool CommitPrefabBuildByTagWithRotation(
        FGameplayTag BuildingTag,
        FIntPoint Origin,
        int32 RotationIndex,
        int32 AgentID
    );

    // New preferred prefab path: commits explicit 3D blocks with rotation
    // anchored at the supplied voxel origin, so prefabs can stack on top of
    // existing placed cubes instead of always starting at Z = 0.
    UFUNCTION(BlueprintCallable)
    bool CommitPrefabBuildByTagWithRotation3D(
        FGameplayTag BuildingTag,
        FIntVector Origin,
        int32 RotationIndex,
        int32 AgentID
    );

    UFUNCTION(BlueprintPure)
    FString GetLastCommitResultMessage() const { return LastCommitResultMessage; }

    // ================================
    // Phase 1: Dig State
    // ================================

    /** Current dig tool settings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build|Dig")
    FDigSettings DigSettings;

    /** Grid position the dig ghost is currently targeting. */
    UPROPERTY(BlueprintReadOnly, Category = "Build|Dig")
    FIntVector CurrentDigTarget = FIntVector::ZeroValue;

    /** World position of the current dig target. */
    UPROPERTY(BlueprintReadOnly, Category = "Build|Dig")
    FVector CurrentDigWorldPosition = FVector::ZeroVector;

    /** True if the current dig target is valid. */
    UPROPERTY(BlueprintReadOnly, Category = "Build|Dig")
    bool bDigTargetValid = false;

    /** Time remaining before the next dig action is allowed. */
    UPROPERTY(BlueprintReadOnly, Category = "Build|Dig")
    float DigCooldownRemaining = 0.0f;

    // ================================
    // Phase 2: Resource X-Ray Mode
    // ================================

    /**
     * When true, DigGhostActor renders subsurface resource node outlines
     * through terrain. Toggled by holding Ctrl while in Dig tool mode.
     * Read by DigGhostActor::Tick() to show/hide X-ray wireframes.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Build|Dig")
    bool bXRayResourcesActive = false;

    /** Stack of dig operations for undo. Most recent dig is at the end. */
    UPROPERTY(SaveGame)
    TArray<FDigUndoEntry> DigUndoStack;

    /** Maximum number of dig operations stored in undo stack. */
    UPROPERTY(EditDefaultsOnly, Category = "Build|Dig")
    int32 MaxDigUndoEntries = 50;

    /** Push a dig result onto the undo stack. */
    void PushDigUndo(const FDigResult& Result, int64 SimTick)
    {
        FDigUndoEntry Entry;
        Entry.DigResult = Result;
        Entry.SimTick = SimTick;

        DigUndoStack.Add(MoveTemp(Entry));

        while (DigUndoStack.Num() > MaxDigUndoEntries)
        {
            DigUndoStack.RemoveAt(0);
        }
    }

    /** Pop the most recent dig from the undo stack. */
    bool PopDigUndo(FDigUndoEntry& OutEntry)
    {
        if (DigUndoStack.Num() == 0)
        {
            return false;
        }

        OutEntry = DigUndoStack.Last();
        DigUndoStack.RemoveAt(DigUndoStack.Num() - 1);
        return true;
    }

    /** Clear the dig undo stack. */
    void ClearDigUndoStack()
    {
        DigUndoStack.Empty();
    }

    /** Update dig cooldown. */
    void TickDigCooldown(float DeltaTime)
    {
        if (DigCooldownRemaining > 0.0f)
        {
            DigCooldownRemaining = FMath::Max(0.0f, DigCooldownRemaining - DeltaTime);
        }
    }

    /** Check if a dig action is off cooldown. */
    bool CanDigNow() const
    {
        return DigCooldownRemaining <= 0.0f;
    }

    /** Start the dig cooldown after a successful dig. */
    void StartDigCooldown()
    {
        DigCooldownRemaining = DigSettings.DigCooldown;
    }

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

    // New canonical explicit prefab builder with full voxel origin.
    bool BuildExplicitPrefabBlockList3D(
        FGameplayTag BuildingTag,
        FIntVector Origin,
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
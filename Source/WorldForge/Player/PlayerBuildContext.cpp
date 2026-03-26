#include "PlayerBuildContext.h"
#include "VoxelConstructionSystem.h"
#include "BuildingTagHelper.h"
#include "Engine/World.h"

void UPlayerBuildContext::AddCell(FIntPoint Cell)
{
    // Prevent duplicate cells
    if (!SelectedCells.Contains(Cell))
    {
        SelectedCells.Add(Cell);
    }
}

void UPlayerBuildContext::ClearSelection()
{
    SelectedCells.Empty();
}

void UPlayerBuildContext::SetMaterial(FGameplayTag MaterialTag)
{
    SelectedMaterial = MaterialTag;
}

void UPlayerBuildContext::SetActiveSettlement(int32 SettlementID)
{
    ActiveSettlementID = SettlementID;
}

bool UPlayerBuildContext::CommitBuild(int32 AgentID)
{
    // Backward-compatible wrapper for the original prototype path.
    return CommitCustomBuild(AgentID);
}

bool UPlayerBuildContext::CommitCustomBuild(int32 AgentID)
{
    UWorld* World = GetWorld();

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: World null"));
        return false;
    }

    if (SelectedCells.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: No selected cells"));
        return false;
    }

    if (ActiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Invalid SettlementID"));
        return false;
    }

    UVoxelConstructionSystem* Construction = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!Construction)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Construction system missing"));
        return false;
    }

    const int32 TaskID = Construction->StartCustomBuildTask(
        ActiveSettlementID,
        SelectedCells,
        SelectedMaterial,
        AgentID
    );

    if (TaskID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Task creation failed"));
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PlayerBuildContext] Custom build committed: %d cells, material=%s"),
        SelectedCells.Num(),
        *SelectedMaterial.ToString()
    );

    ClearSelection();
    return true;
}

bool UPlayerBuildContext::CommitPrefabBuild(
    EBuildingPurpose Purpose,
    int32 EraIndex,
    FIntPoint Origin,
    int32 AgentID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: World null"));
        return false;
    }

    if (ActiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Invalid SettlementID"));
        return false;
    }

    UVoxelConstructionSystem* Construction = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!Construction)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Construction system missing"));
        return false;
    }

    const FGameplayTag BuildingTag = UBuildingTagHelper::GetEraBuildingTag(Purpose, EraIndex);
    if (!BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Invalid building tag"));
        return false;
    }

    const int32 TaskID = Construction->StartBuildTask(
        ActiveSettlementID,
        BuildingTag,
        Origin,
        AgentID
    );

    if (TaskID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Task creation failed"));
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PlayerBuildContext] Prefab build committed: Tag=%s Era=%d Origin=(%d,%d)"),
        *BuildingTag.ToString(),
        EraIndex,
        Origin.X,
        Origin.Y
    );

    return true;
}
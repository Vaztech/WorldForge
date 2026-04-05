#include "PlayerBuildContext.h"
#include "VoxelConstructionSystem.h"
#include "BuildingTagHelper.h"
#include "Engine/World.h"

void UPlayerBuildContext::AddCell(FIntPoint Cell)
{
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

void UPlayerBuildContext::SetLastCommitResultMessage(const FString& Message)
{
    LastCommitResultMessage = Message;
}

bool UPlayerBuildContext::CommitBuild(int32 AgentID)
{
    return CommitCustomBuild(AgentID);
}

bool UPlayerBuildContext::CommitCustomBuild(int32 AgentID)
{
    UWorld* World = GetWorld();

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: World null"));
        SetLastCommitResultMessage(TEXT("Custom build failed: world unavailable."));
        return false;
    }

    if (SelectedCells.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: No selected cells"));
        SetLastCommitResultMessage(TEXT("Custom build failed: no cells selected."));
        return false;
    }

    if (ActiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Invalid SettlementID"));
        SetLastCommitResultMessage(TEXT("Custom build failed: invalid settlement."));
        return false;
    }

    UVoxelConstructionSystem* Construction = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!Construction)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Construction system missing"));
        SetLastCommitResultMessage(TEXT("Custom build failed: construction system missing."));
        return false;
    }

    const int32 CommittedCellCount = SelectedCells.Num();

    const int32 TaskID = Construction->StartCustomBuildTask(
        ActiveSettlementID,
        SelectedCells,
        SelectedMaterial,
        AgentID
    );

    if (TaskID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitCustomBuild failed: Task creation failed"));
        SetLastCommitResultMessage(TEXT("Custom build failed: task creation was rejected."));
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[PlayerBuildContext] Custom build committed: %d cells, material=%s"),
        CommittedCellCount,
        *SelectedMaterial.ToString()
    );

    SetLastCommitResultMessage(
        FString::Printf(
            TEXT("Custom build committed: %d cells of %s."),
            CommittedCellCount,
            SelectedMaterial.IsValid() ? *SelectedMaterial.ToString() : TEXT("UnknownMaterial")
        )
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
        SetLastCommitResultMessage(TEXT("Prefab build failed: world unavailable."));
        return false;
    }

    if (ActiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Invalid SettlementID"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: invalid settlement."));
        return false;
    }

    UVoxelConstructionSystem* Construction = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!Construction)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Construction system missing"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: construction system missing."));
        return false;
    }

    const FGameplayTag BuildingTag = UBuildingTagHelper::GetEraBuildingTag(Purpose, EraIndex);
    if (!BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuild failed: Invalid building tag"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: invalid building tag."));
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
        SetLastCommitResultMessage(
            FString::Printf(
                TEXT("Could not place %s at (%d, %d). A task may already exist there or requirements were not met."),
                *BuildingTag.ToString(),
                Origin.X,
                Origin.Y
            )
        );
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

    SetLastCommitResultMessage(
        FString::Printf(
            TEXT("Placed %s task at (%d, %d)."),
            *BuildingTag.ToString(),
            Origin.X,
            Origin.Y
        )
    );

    return true;
}
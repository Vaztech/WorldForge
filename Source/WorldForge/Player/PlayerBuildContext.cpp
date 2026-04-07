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
    const FGameplayTag BuildingTag = UBuildingTagHelper::GetEraBuildingTag(Purpose, EraIndex);
    return CommitPrefabBuildByTagWithRotation(BuildingTag, Origin, 0, AgentID);
}

bool UPlayerBuildContext::CommitPrefabBuildByTag(
    FGameplayTag BuildingTag,
    FIntPoint Origin,
    int32 AgentID)
{
    return CommitPrefabBuildByTagWithRotation(BuildingTag, Origin, 0, AgentID);
}

bool UPlayerBuildContext::CommitPrefabBuildByTagWithRotation(
    FGameplayTag BuildingTag,
    FIntPoint Origin,
    int32 RotationIndex,
    int32 AgentID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: World null"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: world unavailable."));
        return false;
    }

    if (ActiveSettlementID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: Invalid SettlementID"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: invalid settlement."));
        return false;
    }

    if (!BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: Invalid building tag"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: invalid building tag."));
        return false;
    }

    UVoxelConstructionSystem* Construction = World->GetSubsystem<UVoxelConstructionSystem>();
    if (!Construction)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: Construction system missing"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: construction system missing."));
        return false;
    }

    TArray<FIntVector> Blocks;
    FGameplayTag MaterialTag;
    if (!BuildExplicitPrefabBlockList(BuildingTag, Origin, RotationIndex, Blocks, MaterialTag))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: Could not build explicit block list"));
        SetLastCommitResultMessage(TEXT("Prefab build failed: could not resolve block layout."));
        return false;
    }

    const int32 TaskID = Construction->StartBuildTaskFromBlocks(
        ActiveSettlementID,
        BuildingTag,
        Blocks,
        MaterialTag,
        AgentID
    );

    if (TaskID < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerBuildContext] CommitPrefabBuildByTagWithRotation failed: Task creation failed"));

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
        TEXT("[PlayerBuildContext] Prefab build committed: Tag=%s Origin=(%d,%d) Rotation=%d Blocks=%d"),
        *BuildingTag.ToString(),
        Origin.X,
        Origin.Y,
        RotationIndex,
        Blocks.Num()
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

bool UPlayerBuildContext::TryParseExplicitPrefabDimensions(
    const FString& Source,
    int32& OutWidth,
    int32& OutDepth,
    int32& OutHeight
) const
{
    OutWidth = 0;
    OutDepth = 0;
    OutHeight = 0;

    const int32 Length = Source.Len();
    for (int32 Start = 0; Start < Length; ++Start)
    {
        if (!FChar::IsDigit(Source[Start]))
        {
            continue;
        }

        int32 Cursor = Start;

        FString A;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            A.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (A.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
        {
            continue;
        }

        ++Cursor;

        FString B;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            B.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (B.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
        {
            continue;
        }

        ++Cursor;

        FString C;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            C.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (C.IsEmpty())
        {
            continue;
        }

        const int32 ParsedWidth = FCString::Atoi(*A);
        const int32 ParsedDepth = FCString::Atoi(*B);
        const int32 ParsedHeight = FCString::Atoi(*C);

        if (ParsedWidth > 0 && ParsedDepth > 0 && ParsedHeight > 0)
        {
            OutWidth = ParsedWidth;
            OutDepth = ParsedDepth;
            OutHeight = ParsedHeight;
            return true;
        }
    }

    return false;
}

FIntPoint UPlayerBuildContext::RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const
{
    const int32 SafeRotation = ((RotationIndex % 4) + 4) % 4;

    switch (SafeRotation)
    {
    case 0:
        return LocalXY;

    case 1:
        return FIntPoint(Depth - 1 - LocalXY.Y, LocalXY.X);

    case 2:
        return FIntPoint(Width - 1 - LocalXY.X, Depth - 1 - LocalXY.Y);

    case 3:
        return FIntPoint(LocalXY.Y, Width - 1 - LocalXY.X);

    default:
        return LocalXY;
    }
}

int32 UPlayerBuildContext::GetLegacyPrefabBlockCount(FGameplayTag BuildingTag) const
{
    const FString S = BuildingTag.ToString();

    int32 TrailingDigitStart = INDEX_NONE;
    for (int32 Index = S.Len() - 1; Index >= 0; --Index)
    {
        if (FChar::IsDigit(S[Index]))
        {
            TrailingDigitStart = Index;
        }
        else
        {
            break;
        }
    }

    if (TrailingDigitStart != INDEX_NONE)
    {
        const FString TrailingDigits = S.Mid(TrailingDigitStart);
        const int32 ParsedCount = FCString::Atoi(*TrailingDigits);
        if (ParsedCount > 0)
        {
            return ParsedCount;
        }
    }

    if (S.Contains(TEXT("Stockpile"))) return 8;
    if (S.Contains(TEXT("Kitchen"))) return 16;
    if (S.Contains(TEXT("Carpenter"))) return 20;
    if (S.Contains(TEXT("Barracks"))) return 24;
    if (S.Contains(TEXT("Shrine"))) return 12;

    return 10;
}

bool UPlayerBuildContext::BuildExplicitPrefabBlockList(
    FGameplayTag BuildingTag,
    FIntPoint Origin,
    int32 RotationIndex,
    TArray<FIntVector>& OutBlocks,
    FGameplayTag& OutMaterialTag
) const
{
    OutBlocks.Reset();
    OutMaterialTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    if (!BuildingTag.IsValid())
    {
        return false;
    }

    int32 Width = 0;
    int32 Depth = 0;
    int32 Height = 0;

    if (TryParseExplicitPrefabDimensions(BuildingTag.ToString(), Width, Depth, Height))
    {
        OutBlocks.Reserve(Width * Depth * Height);

        for (int32 Z = 0; Z < Height; ++Z)
        {
            for (int32 Y = 0; Y < Depth; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const FIntPoint RotatedLocal = RotateLocalCell(FIntPoint(X, Y), RotationIndex, Width, Depth);
                    OutBlocks.Add(FIntVector(
                        Origin.X + RotatedLocal.X,
                        Origin.Y + RotatedLocal.Y,
                        Z
                    ));
                }
            }
        }

        OutBlocks.Sort([](const FIntVector& A, const FIntVector& B)
            {
                if (A.Z != B.Z) return A.Z < B.Z;
                if (A.Y != B.Y) return A.Y < B.Y;
                return A.X < B.X;
            });

        return OutBlocks.Num() > 0;
    }

    const int32 RequiredBlocks = GetLegacyPrefabBlockCount(BuildingTag);
    OutBlocks.Reserve(RequiredBlocks);

    for (int32 BlockNumber = 0; BlockNumber < RequiredBlocks; ++BlockNumber)
    {
        const int32 Layer = BlockNumber / 4;
        const int32 BlockIndexWithinLayer = BlockNumber % 4;

        FIntPoint LocalOffset = FIntPoint::ZeroValue;
        switch (BlockIndexWithinLayer)
        {
        case 0: LocalOffset = FIntPoint(0, 0); break;
        case 1: LocalOffset = FIntPoint(1, 0); break;
        case 2: LocalOffset = FIntPoint(0, 1); break;
        case 3: LocalOffset = FIntPoint(1, 1); break;
        default: break;
        }

        const FIntPoint RotatedLocal = RotateLocalCell(LocalOffset, RotationIndex, 2, 2);

        OutBlocks.Add(FIntVector(
            Origin.X + RotatedLocal.X,
            Origin.Y + RotatedLocal.Y,
            Layer
        ));
    }

    return OutBlocks.Num() > 0;
}
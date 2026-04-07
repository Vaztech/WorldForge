// Source/WorldForge/Player/BuildGhostActor.cpp
#include "BuildGhostActor.h"
#include "VoxelWorldManager.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ABuildGhostActor::ABuildGhostActor()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMesh"));
    GhostMesh->SetupAttachment(Root);
    GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GhostMesh->SetCastShadow(false);
    GhostMesh->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeFinder.Succeeded())
    {
        GhostMesh->SetStaticMesh(CubeFinder.Object);
    }
}

void ABuildGhostActor::BeginPlay()
{
    Super::BeginPlay();

    UMaterialInterface* BaseMat = GhostMesh->GetMaterial(0);
    if (BaseMat)
    {
        GhostMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (GhostMaterial)
        {
            GhostMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
            GhostMesh->SetMaterial(0, GhostMaterial);
        }
    }

    if (BaseMat)
    {
        CellValidMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (CellValidMaterial)
        {
            CellValidMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity));
            CellValidMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
        }

        CellCursorMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (CellCursorMaterial)
        {
            CellCursorMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.2f, 0.5f, 1.0f, GhostOpacity));
            CellCursorMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
        }
    }
}

void ABuildGhostActor::SetGhostState(EBuildGhostState State)
{
    CurrentState = State;

    if (State == EBuildGhostState::Hidden)
    {
        GhostMesh->SetVisibility(false);
        for (int32 i = 0; i < ActiveHighlightCount; ++i)
        {
            if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
            {
                CellHighlightPool[i]->SetVisibility(false);
            }
        }
        return;
    }

    if (GhostMaterial)
    {
        GhostMaterial->SetVectorParameterValue(FName("BaseColor"), GetColorForState(State));
    }
}

FLinearColor ABuildGhostActor::GetColorForState(EBuildGhostState State) const
{
    switch (State)
    {
    case EBuildGhostState::Valid:
        return FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity);
    case EBuildGhostState::Invalid:
        return FLinearColor(0.9f, 0.1f, 0.1f, GhostOpacity);
    default:
        return FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
    }
}

bool ABuildGhostActor::TryParseExplicitPrefabDimensions(
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

FIntPoint ABuildGhostActor::RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const
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

int32 ABuildGhostActor::GetLegacyPrefabBlockCount(FGameplayTag BuildingTag) const
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

void ABuildGhostActor::UpdatePrefabGhost(
    FVector WorldPosition,
    int32 RotationIndex,
    FGameplayTag BuildingTag,
    UVoxelWorldManager* VoxelManager,
    float SurfaceZ)
{
    if (!VoxelManager)
    {
        return;
    }

    GhostMesh->SetVisibility(false);

    TArray<FIntVector> PreviewBlocks;

    int32 Width = 0;
    int32 Depth = 0;
    int32 Height = 0;

    if (TryParseExplicitPrefabDimensions(BuildingTag.ToString(), Width, Depth, Height))
    {
        PreviewBlocks.Reserve(Width * Depth * Height);

        for (int32 Z = 0; Z < Height; ++Z)
        {
            for (int32 Y = 0; Y < Depth; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const FIntPoint RotatedLocal = RotateLocalCell(FIntPoint(X, Y), RotationIndex, Width, Depth);
                    PreviewBlocks.Add(FIntVector(RotatedLocal.X, RotatedLocal.Y, Z));
                }
            }
        }
    }
    else
    {
        const int32 LegacyCount = GetLegacyPrefabBlockCount(BuildingTag);
        PreviewBlocks.Reserve(LegacyCount);

        for (int32 BlockNumber = 0; BlockNumber < LegacyCount; ++BlockNumber)
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
            PreviewBlocks.Add(FIntVector(RotatedLocal.X, RotatedLocal.Y, Layer));
        }
    }

    EnsurePoolSize(PreviewBlocks.Num());

    const FIntPoint OriginCell = VoxelManager->WorldToGrid(WorldPosition);

    int32 Idx = 0;
    for (const FIntVector& LocalBlock : PreviewBlocks)
    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            FVector Pos = VoxelManager->GridToWorld(
                FIntPoint(OriginCell.X + LocalBlock.X, OriginCell.Y + LocalBlock.Y),
                LocalBlock.Z
            );

            Pos.Z = SurfaceZ + (LocalBlock.Z * VoxelManager->VoxelSize);

            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);
            Comp->SetWorldScale3D(FVector(1.0f));

            if (CellValidMaterial)
            {
                Comp->SetMaterial(0, CellValidMaterial);
            }
        }
        ++Idx;
    }

    for (int32 i = Idx; i < ActiveHighlightCount; ++i)
    {
        if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
        }
    }

    ActiveHighlightCount = Idx;
}

void ABuildGhostActor::UpdateCustomDrawCells(
    const TArray<FIntPoint>& SelectedCells,
    FIntPoint CursorCell,
    UVoxelWorldManager* VoxelManager,
    float SurfaceZ)
{
    if (!VoxelManager) return;

    GhostMesh->SetVisibility(false);

    const int32 TotalNeeded = SelectedCells.Num() + 1;
    EnsurePoolSize(TotalNeeded);

    int32 Idx = 0;

    for (const FIntPoint& Cell : SelectedCells)
    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            FVector Pos = VoxelManager->GridToWorld(Cell, 0);
            Pos.Z = SurfaceZ;
            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CellValidMaterial)
            {
                Comp->SetMaterial(0, CellValidMaterial);
            }

            // Needed fix: keep free-build ghost as full voxel cubes, not flattened slabs.
            Comp->SetWorldScale3D(FVector(1.0f));
        }
        ++Idx;
    }

    {
        UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
        if (Comp)
        {
            FVector Pos = VoxelManager->GridToWorld(CursorCell, 0);
            Pos.Z = SurfaceZ;
            Comp->SetWorldLocation(Pos);
            Comp->SetVisibility(true);

            if (CellCursorMaterial)
            {
                Comp->SetMaterial(0, CellCursorMaterial);
            }

            // Needed fix: cursor ghost is also a full voxel cube.
            Comp->SetWorldScale3D(FVector(1.0f));
        }
        ++Idx;
    }

    for (int32 i = Idx; i < ActiveHighlightCount; ++i)
    {
        if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
        }
    }

    ActiveHighlightCount = Idx;
}

void ABuildGhostActor::ClearAllHighlights()
{
    for (int32 i = 0; i < CellHighlightPool.Num(); ++i)
    {
        if (CellHighlightPool[i])
        {
            CellHighlightPool[i]->SetVisibility(false);
        }
    }
    ActiveHighlightCount = 0;

    GhostMesh->SetVisibility(false);
}

UStaticMeshComponent* ABuildGhostActor::GetOrCreateHighlight(int32 Index)
{
    if (CellHighlightPool.IsValidIndex(Index) && CellHighlightPool[Index])
    {
        return CellHighlightPool[Index];
    }

    UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this);
    if (!NewComp) return nullptr;

    NewComp->SetupAttachment(GetRootComponent());
    NewComp->RegisterComponent();
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComp->SetCastShadow(false);
    NewComp->SetVisibility(false);

    if (GhostMesh && GhostMesh->GetStaticMesh())
    {
        NewComp->SetStaticMesh(GhostMesh->GetStaticMesh());
    }

    // Default to full voxel cube. Custom/prefab update functions can override if needed.
    NewComp->SetWorldScale3D(FVector(1.0f));

    if (Index < CellHighlightPool.Num())
    {
        CellHighlightPool[Index] = NewComp;
    }
    else
    {
        CellHighlightPool.Add(NewComp);
    }

    return NewComp;
}

void ABuildGhostActor::EnsurePoolSize(int32 RequiredSize)
{
    while (CellHighlightPool.Num() < RequiredSize)
    {
        CellHighlightPool.Add(nullptr);
    }
}
// CubeTerrainTypes.h — CUBE-TERRAIN-01B
// Lightweight data types for the parallel cube terrain prototype path.
// No rendering, no UObjects. Plain structs and enums only.
// Smooth terrain remains default and is not affected by this header.

#pragma once

#include "CoreMinimal.h"
#include "CubeTerrainTypes.generated.h"

// ─────────────────────────────────────────────────────────────────
// Block type enum — mirrors the visual layers of WorldForgeTerrainGenerator
// so future rendering (01D) can map 1:1 to existing material layers.
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EWorldForgeCubeBlockType : uint8
{
    Air       UMETA(DisplayName = "Air"),
    Grass     UMETA(DisplayName = "Grass"),
    Dirt      UMETA(DisplayName = "Dirt"),
    Sand      UMETA(DisplayName = "Sand"),
    Rock      UMETA(DisplayName = "Rock"),
    Snow      UMETA(DisplayName = "Snow"),
    Water     UMETA(DisplayName = "Water"),
    DeepRock  UMETA(DisplayName = "Deep Rock"),
    Lava      UMETA(DisplayName = "Lava"),
    Basalt    UMETA(DisplayName = "Basalt")
};

// ─────────────────────────────────────────────────────────────────
// Single grid cell (X, Y, Z) with block type and removed flag.
// Kept trivial — no UObject, no allocation per cell.
// ─────────────────────────────────────────────────────────────────

USTRUCT()
struct FWorldForgeCubeCell
{
    GENERATED_BODY()

    EWorldForgeCubeBlockType BlockType = EWorldForgeCubeBlockType::Air;
    bool bRemoved = false;
};

// ─────────────────────────────────────────────────────────────────
// Chunk coordinate key — one chunk per tile in the world grid.
// ─────────────────────────────────────────────────────────────────

USTRUCT()
struct FWorldForgeCubeChunkKey
{
    GENERATED_BODY()

    int32 ChunkX = 0;
    int32 ChunkY = 0;

    bool operator==(const FWorldForgeCubeChunkKey& Other) const
    {
        return ChunkX == Other.ChunkX && ChunkY == Other.ChunkY;
    }
};

FORCEINLINE uint32 GetTypeHash(const FWorldForgeCubeChunkKey& Key)
{
    return HashCombine(GetTypeHash(Key.ChunkX), GetTypeHash(Key.ChunkY));
}

// ─────────────────────────────────────────────────────────────────
// Chunk data — a vertical column of cells indexed by Z layer.
// ChunkSizeX/Y match the voxel chunk size; Depth is the Z column height.
// Cells are stored flat: Index = Z * ChunkSizeX * ChunkSizeY + Y * ChunkSizeX + X
// (local X/Y within chunk, global Z).
// ─────────────────────────────────────────────────────────────────

USTRUCT()
struct FWorldForgeCubeChunkData
{
    GENERATED_BODY()

    int32 ChunkSizeX = 0;
    int32 ChunkSizeY = 0;
    int32 Depth      = 0;

    TArray<FWorldForgeCubeCell> Cells;

    void Init(int32 InSizeX, int32 InSizeY, int32 InDepth)
    {
        ChunkSizeX = InSizeX;
        ChunkSizeY = InSizeY;
        Depth      = InDepth;
        Cells.SetNumZeroed(InSizeX * InSizeY * InDepth);
    }

    bool IsValid() const
    {
        return ChunkSizeX > 0 && ChunkSizeY > 0 && Depth > 0 &&
               Cells.Num() == (ChunkSizeX * ChunkSizeY * Depth);
    }

    int32 CellIndex(int32 LocalX, int32 LocalY, int32 Z) const
    {
        return Z * ChunkSizeX * ChunkSizeY + LocalY * ChunkSizeX + LocalX;
    }

    FWorldForgeCubeCell* GetCell(int32 LocalX, int32 LocalY, int32 Z)
    {
        const int32 Idx = CellIndex(LocalX, LocalY, Z);
        return Cells.IsValidIndex(Idx) ? &Cells[Idx] : nullptr;
    }

    const FWorldForgeCubeCell* GetCell(int32 LocalX, int32 LocalY, int32 Z) const
    {
        const int32 Idx = CellIndex(LocalX, LocalY, Z);
        return Cells.IsValidIndex(Idx) ? &Cells[Idx] : nullptr;
    }
};

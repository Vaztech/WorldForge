#pragma once

// ════════════════════════════════════════════════════════════════════
// TerrainModificationLayer.h — v5.0
//
// HYBRID CHUNKING MODIFICATION PATH
//
// Core data structures for the sparse terrain modification system.
// Instead of saving entire chunks or maintaining a flat set of
// removed-block coordinates, modifications are stored as SDF brush
// volumes indexed by chunk coordinate.
//
// At density-evaluation time the compositor formula is:
//
//   FinalDensity = BaseCompositor(x, y, z)
//                + ChunkModifications.SampleAt(x, y, z)
//
// Positive brush contributions push density toward air (carving).
// Negative contributions push toward solid (filling / building).
//
// Only chunks that have been modified are stored.  Unmodified chunks
// cost zero memory — their shape is entirely deterministic from the
// seed and tile elevation data.
//
// FILE LOCATION: Source/WorldForge/Voxel/TerrainModificationLayer.h
// ════════════════════════════════════════════════════════════════════

#include "CoreMinimal.h"
#include "TerrainModificationLayer.generated.h"

// ─── Brush shape enum ───────────────────────────────────────────

UENUM(BlueprintType)
enum class ESDFBrushShape : uint8
{
    /** Spherical carve — general-purpose digging and mining. */
    Sphere    UMETA(DisplayName = "Sphere"),

    /** Vertical cylinder — shaft digging on slopes. */
    Cylinder  UMETA(DisplayName = "Cylinder"),

    /** Axis-aligned box — rectangular excavation / room carving. */
    Box       UMETA(DisplayName = "Box")
};

// ─── Brush operation mode ───────────────────────────────────────

UENUM(BlueprintType)
enum class ESDFBrushMode : uint8
{
    /** Subtractive: carves terrain away (pushes density positive = air). */
    Carve  UMETA(DisplayName = "Carve"),

    /** Additive: fills terrain back in (pushes density negative = solid). */
    Fill   UMETA(DisplayName = "Fill")
};

// ═══════════════════════════════════════════════════════════════════
// FSDFBrush — A single SDF modification volume
// ═══════════════════════════════════════════════════════════════════
//
// Each brush is a signed-distance primitive positioned in voxel
// coordinates.  Evaluate() returns a positive value where the brush
// wants to push density toward air (carving), or negative where it
// fills (building).  The magnitude determines how strongly the
// isosurface is displaced.
//
// Brushes are designed for Transvoxel smooth meshing — the falloff
// produces rounded, natural cave walls rather than binary on/off
// blocks.
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct WORLDFORGE_API FSDFBrush
{
    GENERATED_BODY()

    // ── Identity ──

    /** Unique ID for undo / removal.  Assigned by the modification layer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int64 BrushID = 0;

    // ── Geometry ──

    /** Center of the brush in voxel-space coordinates. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector Center = FVector::ZeroVector;

    /** Shape of the SDF primitive. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    ESDFBrushShape Shape = ESDFBrushShape::Sphere;

    /** Operation mode: carve (dig) or fill (build). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    ESDFBrushMode Mode = ESDFBrushMode::Carve;

    /**
     * Primary radius in voxels (Sphere / Cylinder).
     * For Box, this is unused — use HalfExtents instead.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.1"))
    float Radius = 2.0f;

    /**
     * Half-height in voxels (Cylinder only).
     * The cylinder extends from Center.Z - HalfHeight to Center.Z + HalfHeight.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.1"))
    float HalfHeight = 2.5f;

    /**
     * Half-extents in voxels (Box only).
     * The box spans Center ± HalfExtents on each axis.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector HalfExtents = FVector(1.5f, 1.5f, 2.5f);

    /**
     * Carve/fill strength.  Higher = sharper transition at the
     * brush boundary.  1.0 is the default for standard digging.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.01"))
    float Strength = 1.0f;

    /**
     * Falloff distance in voxels outside the brush's core radius.
     * Creates smooth blending between carved and solid regions.
     * 0.0 = hard edge (not recommended for smooth terrain).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.0"))
    float FalloffVoxels = 0.5f;

    // ── Evaluation ──

    /**
     * Evaluate this brush's SDF contribution at the given voxel-space point.
     *
     * Returns:
     *   Positive = push toward air  (Carve mode, inside brush volume)
     *   Negative = push toward solid (Fill mode, inside brush volume)
     *   Zero     = no effect (outside brush influence)
     */
    float Evaluate(float X, float Y, float Z) const
    {
        float RawSDF = 0.0f;

        switch (Shape)
        {
        case ESDFBrushShape::Sphere:
            RawSDF = EvaluateSphere(X, Y, Z);
            break;

        case ESDFBrushShape::Cylinder:
            RawSDF = EvaluateCylinder(X, Y, Z);
            break;

        case ESDFBrushShape::Box:
            RawSDF = EvaluateBox(X, Y, Z);
            break;
        }

        return (Mode == ESDFBrushMode::Carve) ? RawSDF : -RawSDF;
    }

    /**
     * Get the axis-aligned bounding box of this brush in voxel-space.
     * Used for chunk-overlap testing at insertion time.
     */
    FBox GetBounds() const
    {
        float Extent = FalloffVoxels;

        switch (Shape)
        {
        case ESDFBrushShape::Sphere:
            Extent += Radius;
            return FBox(Center - FVector(Extent), Center + FVector(Extent));

        case ESDFBrushShape::Cylinder:
            Extent += Radius;
            return FBox(
                FVector(Center.X - Extent, Center.Y - Extent, Center.Z - HalfHeight - FalloffVoxels),
                FVector(Center.X + Extent, Center.Y + Extent, Center.Z + HalfHeight + FalloffVoxels)
            );

        case ESDFBrushShape::Box:
            return FBox(
                Center - HalfExtents - FVector(FalloffVoxels),
                Center + HalfExtents + FVector(FalloffVoxels)
            );
        }

        return FBox(Center - FVector(Extent), Center + FVector(Extent));
    }

private:
    // ── Shape evaluators ──
    // All return positive values inside the brush (= carve zone).
    // The strength * smoothstep pattern produces smooth SDF walls
    // that Transvoxel meshes into natural-looking surfaces.

    float EvaluateSphere(float X, float Y, float Z) const
    {
        const float DX = X - Center.X;
        const float DY = Y - Center.Y;
        const float DZ = Z - Center.Z;
        const float Dist = FMath::Sqrt(DX * DX + DY * DY + DZ * DZ);

        if (Dist >= Radius + FalloffVoxels)
        {
            return 0.0f;
        }

        if (Dist <= Radius)
        {
            // Fully inside the sphere — max carve
            return Strength;
        }

        // Falloff zone: smooth blend from Strength → 0
        const float T = (Dist - Radius) / FMath::Max(KINDA_SMALL_NUMBER, FalloffVoxels);
        return Strength * (1.0f - FMath::SmoothStep(0.0f, 1.0f, T));
    }

    float EvaluateCylinder(float X, float Y, float Z) const
    {
        // Radial distance in XY plane
        const float DX = X - Center.X;
        const float DY = Y - Center.Y;
        const float RadialDist = FMath::Sqrt(DX * DX + DY * DY);

        // Vertical distance from cylinder center
        const float VerticalDist = FMath::Abs(Z - Center.Z);

        // Outside bounding volume entirely?
        if (RadialDist >= Radius + FalloffVoxels || VerticalDist >= HalfHeight + FalloffVoxels)
        {
            return 0.0f;
        }

        // Compute falloff for each axis independently, take the minimum
        float RadialFactor = 1.0f;
        if (RadialDist > Radius)
        {
            const float T = (RadialDist - Radius) / FMath::Max(KINDA_SMALL_NUMBER, FalloffVoxels);
            RadialFactor = 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
        }

        float VerticalFactor = 1.0f;
        if (VerticalDist > HalfHeight)
        {
            const float T = (VerticalDist - HalfHeight) / FMath::Max(KINDA_SMALL_NUMBER, FalloffVoxels);
            VerticalFactor = 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
        }

        return Strength * FMath::Min(RadialFactor, VerticalFactor);
    }

    float EvaluateBox(float X, float Y, float Z) const
    {
        const float DX = FMath::Abs(X - Center.X);
        const float DY = FMath::Abs(Y - Center.Y);
        const float DZ = FMath::Abs(Z - Center.Z);

        // Outside bounding volume entirely?
        if (DX >= HalfExtents.X + FalloffVoxels ||
            DY >= HalfExtents.Y + FalloffVoxels ||
            DZ >= HalfExtents.Z + FalloffVoxels)
        {
            return 0.0f;
        }

        // Per-axis falloff
        auto AxisFactor = [this](float AxisDist, float AxisHalf) -> float
        {
            if (AxisDist <= AxisHalf)
            {
                return 1.0f;
            }
            const float T = (AxisDist - AxisHalf) / FMath::Max(KINDA_SMALL_NUMBER, FalloffVoxels);
            return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
        };

        const float FX = AxisFactor(DX, HalfExtents.X);
        const float FY = AxisFactor(DY, HalfExtents.Y);
        const float FZ = AxisFactor(DZ, HalfExtents.Z);

        return Strength * FMath::Min3(FX, FY, FZ);
    }
};

// ═══════════════════════════════════════════════════════════════════
// FChunkModificationLayer — Per-chunk brush container
// ═══════════════════════════════════════════════════════════════════
//
// Each modified chunk holds a list of SDF brushes that have been
// applied to it (mining, digging, building).  Unmodified chunks
// have no entry in the parent TMap — zero cost.
//
// A single brush may be stored in multiple chunk layers if its
// bounding box overlaps chunk boundaries.  This is handled at
// insertion time so that SampleTotalCarve() only needs to query
// the chunk containing the sample point — no neighbor lookups.
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct WORLDFORGE_API FChunkModificationLayer
{
    GENERATED_BODY()

    /** All SDF brushes affecting this chunk. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FSDFBrush> Brushes;

    /**
     * Evaluate the total modification density at a voxel-space point.
     *
     * Iterates all brushes in this chunk and sums their contributions.
     * Positive total = net carve toward air.
     * Negative total = net fill toward solid.
     *
     * This is O(M) where M = number of brushes in THIS chunk — not
     * the total modification count across the entire world.
     */
    float SampleTotalCarve(float X, float Y, float Z) const
    {
        float Total = 0.0f;

        for (const FSDFBrush& Brush : Brushes)
        {
            Total += Brush.Evaluate(X, Y, Z);
        }

        return Total;
    }

    /** True if this chunk has no modifications. */
    bool IsEmpty() const
    {
        return Brushes.Num() == 0;
    }

    /** Add a brush to this chunk. */
    void AddBrush(const FSDFBrush& Brush)
    {
        Brushes.Add(Brush);
    }

    /**
     * Remove a brush by ID.
     * Returns true if found and removed.
     */
    bool RemoveBrushByID(int64 BrushID)
    {
        const int32 Removed = Brushes.RemoveAll(
            [BrushID](const FSDFBrush& B) { return B.BrushID == BrushID; }
        );
        return Removed > 0;
    }

    /**
     * Remove all brushes whose centers are within SearchRadius of
     * the given position.  Used for RestoreBlockAt undo.
     */
    int32 RemoveBrushesNear(const FVector& Position, float SearchRadius)
    {
        const float RadiusSq = SearchRadius * SearchRadius;
        return Brushes.RemoveAll(
            [&Position, RadiusSq](const FSDFBrush& B)
            {
                return FVector::DistSquared(B.Center, Position) <= RadiusSq;
            }
        );
    }

    /** Check if any brush with the given ID exists in this chunk. */
    bool ContainsBrushID(int64 BrushID) const
    {
        for (const FSDFBrush& B : Brushes)
        {
            if (B.BrushID == BrushID)
            {
                return true;
            }
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════════
// FTerrainModificationMap — World-level chunk-indexed modification store
// ═══════════════════════════════════════════════════════════════════
//
// Top-level container wrapping the TMap.  Provides the public API
// for adding/removing brushes and sampling modifications at arbitrary
// voxel-space coordinates.
//
// This struct lives inside FWorldForgeTerrainSnapshot so it travels
// with the generator and save system automatically.
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct WORLDFORGE_API FTerrainModificationMap
{
    GENERATED_BODY()

    /** Chunk-indexed modification layers.  Only dirty chunks have entries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FIntPoint, FChunkModificationLayer> ChunkLayers;

    /** Monotonically increasing brush ID counter for unique IDs. */
    UPROPERTY(SaveGame)
    int64 NextBrushID = 1;

    // ── Grid parameters (set once during snapshot construction) ──

    UPROPERTY()
    int32 ChunkSizeVoxels = 32;

    UPROPERTY()
    int32 GridWidth = 256;

    UPROPERTY()
    int32 GridHeight = 256;

    // ── Public API ──

    /**
     * Set grid parameters.  Must be called before any brush operations.
     */
    void Initialize(int32 InChunkSizeVoxels, int32 InGridWidth, int32 InGridHeight)
    {
        ChunkSizeVoxels = FMath::Max(1, InChunkSizeVoxels);
        GridWidth = FMath::Max(1, InGridWidth);
        GridHeight = FMath::Max(1, InGridHeight);
    }

    /**
     * Add an SDF brush to all chunks it overlaps.
     * Returns the assigned BrushID.
     */
    int64 AddBrush(FSDFBrush Brush)
    {
        Brush.BrushID = NextBrushID++;

        const FBox BrushBounds = Brush.GetBounds();

        // Compute chunk range that the brush AABB overlaps
        const FIntPoint MinChunk = VoxelToChunkKey(
            BrushBounds.Min.X, BrushBounds.Min.Y);
        const FIntPoint MaxChunk = VoxelToChunkKey(
            BrushBounds.Max.X, BrushBounds.Max.Y);

        for (int32 CY = MinChunk.Y; CY <= MaxChunk.Y; ++CY)
        {
            for (int32 CX = MinChunk.X; CX <= MaxChunk.X; ++CX)
            {
                const FIntPoint ChunkKey(CX, CY);
                FChunkModificationLayer& Layer = ChunkLayers.FindOrAdd(ChunkKey);
                Layer.AddBrush(Brush);
            }
        }

        return Brush.BrushID;
    }

    /**
     * Remove a brush by ID from all chunks.
     */
    void RemoveBrush(int64 BrushID)
    {
        TArray<FIntPoint> EmptyKeys;

        for (auto& Pair : ChunkLayers)
        {
            Pair.Value.RemoveBrushByID(BrushID);
            if (Pair.Value.IsEmpty())
            {
                EmptyKeys.Add(Pair.Key);
            }
        }

        // Clean up empty chunks
        for (const FIntPoint& Key : EmptyKeys)
        {
            ChunkLayers.Remove(Key);
        }
    }

    /**
     * Remove all brushes near a world position (for undo / restore).
     * Returns the number of brushes removed.
     */
    int32 RemoveBrushesNear(const FVector& VoxelPosition, float SearchRadius)
    {
        const FIntPoint CenterChunk = VoxelToChunkKey(VoxelPosition.X, VoxelPosition.Y);
        int32 TotalRemoved = 0;

        // Search the center chunk and immediate neighbors (brushes may span boundaries)
        for (int32 DY = -1; DY <= 1; ++DY)
        {
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const FIntPoint Key(CenterChunk.X + DX, CenterChunk.Y + DY);
                FChunkModificationLayer* Layer = ChunkLayers.Find(Key);
                if (Layer)
                {
                    TotalRemoved += Layer->RemoveBrushesNear(VoxelPosition, SearchRadius);
                    if (Layer->IsEmpty())
                    {
                        ChunkLayers.Remove(Key);
                    }
                }
            }
        }

        return TotalRemoved;
    }

    /**
     * Sample the total modification density at a voxel-space point.
     *
     * Looks up the chunk containing (X, Y) and evaluates all brushes
     * in that chunk.  Because brushes are stored in every chunk they
     * overlap, no neighbor lookups are needed.
     *
     * Returns positive = net carve toward air.
     */
    float SampleAt(float X, float Y, float Z) const
    {
        const FIntPoint ChunkKey = VoxelToChunkKey(X, Y);
        const FChunkModificationLayer* Layer = ChunkLayers.Find(ChunkKey);

        if (!Layer)
        {
            return 0.0f;
        }

        return Layer->SampleTotalCarve(X, Y, Z);
    }

    /**
     * Check if any chunks overlapping the given voxel-space bounds
     * contain modifications.  Used by GetValueRangeImpl to decide
     * whether to widen the density range for LOD culling.
     */
    bool HasModificationsInBounds(
        float MinX, float MinY,
        float MaxX, float MaxY) const
    {
        if (ChunkLayers.Num() == 0)
        {
            return false;
        }

        const FIntPoint MinChunk = VoxelToChunkKey(MinX, MinY);
        const FIntPoint MaxChunk = VoxelToChunkKey(MaxX, MaxY);

        for (int32 CY = MinChunk.Y; CY <= MaxChunk.Y; ++CY)
        {
            for (int32 CX = MinChunk.X; CX <= MaxChunk.X; ++CX)
            {
                if (ChunkLayers.Contains(FIntPoint(CX, CY)))
                {
                    return true;
                }
            }
        }

        return false;
    }

    /** Total number of dirty chunks. */
    int32 NumDirtyChunks() const
    {
        return ChunkLayers.Num();
    }

    /** Total number of brushes across all chunks (includes duplicates from overlap). */
    int32 NumTotalBrushEntries() const
    {
        int32 Total = 0;
        for (const auto& Pair : ChunkLayers)
        {
            Total += Pair.Value.Brushes.Num();
        }
        return Total;
    }

    /** Clear all modifications. */
    void Reset()
    {
        ChunkLayers.Empty();
        NextBrushID = 1;
    }

    /** Check if there are any modifications at all. */
    bool IsEmpty() const
    {
        return ChunkLayers.Num() == 0;
    }

private:
    /**
     * Convert voxel-space XY coordinates to the chunk grid key.
     * Uses the same coordinate transform as the terrain generator:
     *   CellX = floor(VoxelX / ChunkSize) + GridWidth/2
     */
    FIntPoint VoxelToChunkKey(float VoxelX, float VoxelY) const
    {
        const float ChunkSizeF = static_cast<float>(FMath::Max(1, ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(GridHeight) * 0.5f;

        const int32 CX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
        const int32 CY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);

        return FIntPoint(CX, CY);
    }
};

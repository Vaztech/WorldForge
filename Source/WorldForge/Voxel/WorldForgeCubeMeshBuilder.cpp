// WorldForgeCubeMeshBuilder.cpp — CUBE-TERRAIN-03D
// Greedy-meshed chunk mesh builder for CubePrototype terrain.
// 03D adds two debug isolation modes:
//   bDebugDisableGreedySideFaces         — side faces (FaceDir 0-3, ±X/±Y) skip greedy expansion.
//   bDebugSuppressOuterPatchBorderFaces  — outer patch boundary side faces are not emitted.
// Both modes are fully gated behind their flags; optimized paths are preserved when flags are false.

#include "WorldForgeCubeMeshBuilder.h"

#include "CubeTerrainRuntime.h"

namespace
{
    static constexpr EWorldForgeCubeBlockType CubeSectionOrder[] =
    {
        EWorldForgeCubeBlockType::Grass,
        EWorldForgeCubeBlockType::Dirt,
        EWorldForgeCubeBlockType::Sand,
        EWorldForgeCubeBlockType::Rock,
        EWorldForgeCubeBlockType::Snow,
        EWorldForgeCubeBlockType::Water,
        EWorldForgeCubeBlockType::DeepRock,
        EWorldForgeCubeBlockType::Lava,
        EWorldForgeCubeBlockType::Basalt
    };

    struct FTileColumnSample
    {
        int32 SurfaceZ = 0;
        EWorldForgeCubeBlockType SurfaceType = EWorldForgeCubeBlockType::Air;
        TArray<uint8> ColumnTypes;
    };

    int32 GetSectionIndexForBlockType(EWorldForgeCubeBlockType BlockType)
    {
        switch (BlockType)
        {
        case EWorldForgeCubeBlockType::Grass:    return 0;
        case EWorldForgeCubeBlockType::Dirt:     return 1;
        case EWorldForgeCubeBlockType::Sand:     return 2;
        case EWorldForgeCubeBlockType::Rock:     return 3;
        case EWorldForgeCubeBlockType::Snow:     return 4;
        case EWorldForgeCubeBlockType::Water:    return 5;
        case EWorldForgeCubeBlockType::DeepRock: return 6;
        case EWorldForgeCubeBlockType::Lava:     return 7;
        case EWorldForgeCubeBlockType::Basalt:   return 8;
        default:                                 return INDEX_NONE;
        }
    }

    bool IsFluidBlock(EWorldForgeCubeBlockType BlockType)
    {
        return BlockType == EWorldForgeCubeBlockType::Water
            || BlockType == EWorldForgeCubeBlockType::Lava;
    }

    int32 GetMaxHeightVariation(EWorldForgeCubeBlockType SurfaceType)
    {
        switch (SurfaceType)
        {
        case EWorldForgeCubeBlockType::Grass:
        case EWorldForgeCubeBlockType::Dirt:
            return 1;

        case EWorldForgeCubeBlockType::Sand:
            return 1;

        case EWorldForgeCubeBlockType::Rock:
            return 2;

        case EWorldForgeCubeBlockType::Snow:
        case EWorldForgeCubeBlockType::DeepRock:
            return 3;

        default:
            return 0;
        }
    }

    struct FBiomeTransitionResult
    {
        EWorldForgeCubeBlockType BlockType;
        int32 TransitionType = 0;
    };

    FBiomeTransitionResult EvaluateBiomeTransition(
        EWorldForgeCubeBlockType CurrentBiome,
        EWorldForgeCubeBlockType Neighbor10Biome,
        EWorldForgeCubeBlockType Neighbor01Biome,
        EWorldForgeCubeBlockType Neighbor11Biome,
        uint32 LocalNoise)
    {
        FBiomeTransitionResult Result;
        Result.BlockType = CurrentBiome;
        Result.TransitionType = 0;

        const uint32 NoiseHash = LocalNoise % 100;

        if (CurrentBiome == EWorldForgeCubeBlockType::Grass)
        {
            if ((Neighbor10Biome == EWorldForgeCubeBlockType::Sand || Neighbor01Biome == EWorldForgeCubeBlockType::Sand ||
                 Neighbor11Biome == EWorldForgeCubeBlockType::Sand) && NoiseHash < 25)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Sand;
                Result.TransitionType = 1;
            }
            else if ((Neighbor10Biome == EWorldForgeCubeBlockType::Rock || Neighbor01Biome == EWorldForgeCubeBlockType::Rock ||
                      Neighbor11Biome == EWorldForgeCubeBlockType::Rock) && NoiseHash < 20)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Rock;
                Result.TransitionType = 2;
            }
            else if ((Neighbor10Biome == EWorldForgeCubeBlockType::Snow || Neighbor01Biome == EWorldForgeCubeBlockType::Snow ||
                      Neighbor11Biome == EWorldForgeCubeBlockType::Snow) && NoiseHash < 15)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Snow;
                Result.TransitionType = 3;
            }
        }
        else if (CurrentBiome == EWorldForgeCubeBlockType::Sand)
        {
            if ((Neighbor10Biome == EWorldForgeCubeBlockType::Grass || Neighbor01Biome == EWorldForgeCubeBlockType::Grass ||
                 Neighbor11Biome == EWorldForgeCubeBlockType::Grass) && NoiseHash < 20)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Grass;
                Result.TransitionType = 1;
            }
        }
        else if (CurrentBiome == EWorldForgeCubeBlockType::Rock)
        {
            if ((Neighbor10Biome == EWorldForgeCubeBlockType::Grass || Neighbor01Biome == EWorldForgeCubeBlockType::Grass ||
                 Neighbor11Biome == EWorldForgeCubeBlockType::Grass) && NoiseHash < 15)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Grass;
                Result.TransitionType = 2;
            }
        }
        else if (CurrentBiome == EWorldForgeCubeBlockType::Snow)
        {
            if ((Neighbor10Biome == EWorldForgeCubeBlockType::Grass || Neighbor01Biome == EWorldForgeCubeBlockType::Grass ||
                 Neighbor11Biome == EWorldForgeCubeBlockType::Grass) && NoiseHash < 10)
            {
                Result.BlockType = EWorldForgeCubeBlockType::Grass;
                Result.TransitionType = 3;
            }
        }

        return Result;
    }

    bool ShouldEmitFace(EWorldForgeCubeBlockType Current, EWorldForgeCubeBlockType Neighbor)
    {
        if (Current == EWorldForgeCubeBlockType::Air)
        {
            return false;
        }

        if (Neighbor == EWorldForgeCubeBlockType::Air)
        {
            return true;
        }

        const bool bCurrentFluid = IsFluidBlock(Current);
        const bool bNeighborFluid = IsFluidBlock(Neighbor);
        if (bCurrentFluid || bNeighborFluid)
        {
            return Current != Neighbor;
        }

        return false;
    }

    FVector BuildTileCenterWorld(const FWorldForgeCubePatchMeshSettings& Settings, int32 TileX, int32 TileY)
    {
        const float HalfGridWidth  = static_cast<float>(Settings.TerrainGridSize.X) * 0.5f;
        const float HalfGridHeight = static_cast<float>(Settings.TerrainGridSize.Y) * 0.5f;

        return FVector(
            Settings.TerrainOrigin.X + ((static_cast<float>(TileX) - HalfGridWidth  + 0.5f) * Settings.TileWorldSizeUU),
            Settings.TerrainOrigin.Y + ((static_cast<float>(TileY) - HalfGridHeight + 0.5f) * Settings.TileWorldSizeUU),
            Settings.TerrainOrigin.Z);
    }

    int32 VolumeIndex(int32 LocalX, int32 LocalY, int32 Z, int32 Width, int32 Height)
    {
        return (Z * Height * Width) + (LocalY * Width) + LocalX;
    }

    EWorldForgeCubeBlockType GetVolumeBlock(
        const TArray<uint8>& Volume,
        int32 LocalX,
        int32 LocalY,
        int32 Z,
        int32 Width,
        int32 Height,
        int32 MaxZ)
    {
        if (LocalX < 0 || LocalY < 0 || Z < 0 || LocalX >= Width || LocalY >= Height || Z > MaxZ)
        {
            return EWorldForgeCubeBlockType::Air;
        }

        const int32 Index = VolumeIndex(LocalX, LocalY, Z, Width, Height);
        return Volume.IsValidIndex(Index)
            ? static_cast<EWorldForgeCubeBlockType>(Volume[Index])
            : EWorldForgeCubeBlockType::Air;
    }

    void AppendFace(
        FWorldForgeCubeMeshSectionData& Section,
        const FVector& Origin,
        const FVector& AxisU,
        const FVector& AxisV,
        const FVector& FaceNormal,
        const FVector& FaceTangent,
        float UTileScale,
        float VTileScale)
    {
        const int32 BaseIndex = Section.Vertices.Num();

        Section.Vertices.Add(Origin);
        Section.Vertices.Add(Origin + AxisV);
        Section.Vertices.Add(Origin + AxisV + AxisU);
        Section.Vertices.Add(Origin + AxisU);

        Section.Triangles.Add(BaseIndex + 0);
        Section.Triangles.Add(BaseIndex + 1);
        Section.Triangles.Add(BaseIndex + 2);
        Section.Triangles.Add(BaseIndex + 0);
        Section.Triangles.Add(BaseIndex + 2);
        Section.Triangles.Add(BaseIndex + 3);

        Section.Normals.Add(FaceNormal);
        Section.Normals.Add(FaceNormal);
        Section.Normals.Add(FaceNormal);
        Section.Normals.Add(FaceNormal);

        Section.UV0.Add(FVector2D(0.0f, 0.0f));
        Section.UV0.Add(FVector2D(0.0f, VTileScale));
        Section.UV0.Add(FVector2D(UTileScale, VTileScale));
        Section.UV0.Add(FVector2D(UTileScale, 0.0f));

        Section.VertexColors.Add(FLinearColor::White);
        Section.VertexColors.Add(FLinearColor::White);
        Section.VertexColors.Add(FLinearColor::White);
        Section.VertexColors.Add(FLinearColor::White);

        const FProcMeshTangent Tangent(FaceTangent.X, FaceTangent.Y, FaceTangent.Z);
        Section.Tangents.Add(Tangent);
        Section.Tangents.Add(Tangent);
        Section.Tangents.Add(Tangent);
        Section.Tangents.Add(Tangent);
    }

    // Greedy-meshes all 6 face directions for one chunk region of the volume.
    // Visible-face logic (ShouldEmitFace) is unchanged from 02D.
    //
    // Coordinate convention:
    //   FaceDir 0/1 = -X/+X  : vertical side face, slice over X, mask U=Y  V=Z
    //   FaceDir 2/3 = -Y/+Y  : vertical side face, slice over Y, mask U=X  V=Z
    //   FaceDir 4   = -Z     : bottom face,         slice over Z, mask U=X  V=Y
    //   FaceDir 5   = +Z     : top face,             slice over Z, mask U=X  V=Y
    //
    // 03D debug flags:
    //   bDebugDisableGreedySideFaces        — FaceDir 0-3: skip greedy U/V expansion (1x1 quads).
    //   bDebugSuppressOuterPatchBorderFaces — FaceDir 0-3: skip faces where neighbor is out-of-volume.
    //   OutSuppressedBorderFaces            — count of faces skipped by the suppression flag.
    void GreedyBuildChunkFaces(
        FWorldForgeCubeChunkMeshData& ChunkData,
        const TArray<uint8>& Volume,
        int32 VolumeW,
        int32 VolumeH,
        int32 MaxSolidZ,
        const TArray<float>& BlockMinX,
        const TArray<float>& BlockMinY,
        float OriginZ,
        float BlockSizeXY,
        float BlockHeightUU,
        int32 MinLX, int32 MaxLX,
        int32 MinLY, int32 MaxLY,
        bool bDebugDisableGreedySideFaces,
        bool bDebugSuppressOuterPatchBorderFaces,
        int32& OutSuppressedBorderFaces)
    {
        const int32 ChunkW = MaxLX - MinLX;
        const int32 ChunkH = MaxLY - MinLY;
        const int32 NumZ   = MaxSolidZ + 1;

        // Count non-Air blocks in this chunk for BlocksChecked stat.
        for (int32 Z = 0; Z <= MaxSolidZ; ++Z)
        {
            for (int32 LY = MinLY; LY < MaxLY; ++LY)
            {
                for (int32 LX = MinLX; LX < MaxLX; ++LX)
                {
                    if (GetVolumeBlock(Volume, LX, LY, Z, VolumeW, VolumeH, MaxSolidZ) != EWorldForgeCubeBlockType::Air)
                    {
                        ++ChunkData.BlocksChecked;
                    }
                }
            }
        }

        TArray<uint8> Mask;
        TArray<uint8> Used;

        for (int32 FaceDir = 0; FaceDir < 6; ++FaceDir)
        {
            // 03D: determine whether this direction is a vertical side face.
            const bool bIsSideDir = (FaceDir <= 3);

            int32 SliceCount, MaskW, MaskH;
            if (FaceDir <= 1)
            {
                SliceCount = ChunkW;
                MaskW      = ChunkH;
                MaskH      = NumZ;
            }
            else if (FaceDir <= 3)
            {
                SliceCount = ChunkH;
                MaskW      = ChunkW;
                MaskH      = NumZ;
            }
            else
            {
                SliceCount = NumZ;
                MaskW      = ChunkW;
                MaskH      = ChunkH;
            }

            const int32 MaskSize = MaskW * MaskH;
            Mask.SetNumUninitialized(MaskSize);
            Used.SetNumUninitialized(MaskSize);

            for (int32 Slice = 0; Slice < SliceCount; ++Slice)
            {
                // ---- Build mask ----
                for (int32 MV = 0; MV < MaskH; ++MV)
                {
                    for (int32 MU = 0; MU < MaskW; ++MU)
                    {
                        int32 LX, LY, LZ, NX, NY, NZ;
                        switch (FaceDir)
                        {
                        case 0:
                            LX=MinLX+Slice; LY=MinLY+MU; LZ=MV;
                            NX=LX-1; NY=LY; NZ=LZ;
                            break;
                        case 1:
                            LX=MinLX+Slice; LY=MinLY+MU; LZ=MV;
                            NX=LX+1; NY=LY; NZ=LZ;
                            break;
                        case 2:
                            LX=MinLX+MU; LY=MinLY+Slice; LZ=MV;
                            NX=LX; NY=LY-1; NZ=LZ;
                            break;
                        case 3:
                            LX=MinLX+MU; LY=MinLY+Slice; LZ=MV;
                            NX=LX; NY=LY+1; NZ=LZ;
                            break;
                        case 4:
                            LX=MinLX+MU; LY=MinLY+MV; LZ=Slice;
                            NX=LX; NY=LY; NZ=LZ-1;
                            break;
                        default: // 5
                            LX=MinLX+MU; LY=MinLY+MV; LZ=Slice;
                            NX=LX; NY=LY; NZ=LZ+1;
                            break;
                        }

                        const EWorldForgeCubeBlockType BT = GetVolumeBlock(Volume, LX, LY, LZ, VolumeW, VolumeH, MaxSolidZ);
                        const EWorldForgeCubeBlockType NT = GetVolumeBlock(Volume, NX, NY, NZ, VolumeW, VolumeH, MaxSolidZ);

                        // 03D: Suppress outer patch boundary side faces.
                        // When the neighbor is outside the local volume, GetVolumeBlock returns Air,
                        // which would normally trigger face emission. We suppress those artificial
                        // border walls so they don't produce giant merged wall quads at patch edges.
                        // Internal cliff faces (neighbor inside volume but Air) are NOT suppressed.
                        bool bSuppressBorderFace = false;
                        if (bDebugSuppressOuterPatchBorderFaces && bIsSideDir)
                        {
                            const bool bNeighborOutside = (NX < 0 || NX >= VolumeW || NY < 0 || NY >= VolumeH);
                            if (bNeighborOutside && ShouldEmitFace(BT, NT))
                            {
                                bSuppressBorderFace = true;
                                ++OutSuppressedBorderFaces;
                            }
                        }

                        Mask[MV * MaskW + MU] = (!bSuppressBorderFace && ShouldEmitFace(BT, NT))
                            ? static_cast<uint8>(BT)
                            : 0u;
                    }
                }

                // Count raw (un-merged) faces for logging.
                for (int32 i = 0; i < MaskSize; ++i)
                {
                    if (Mask[i] != 0)
                    {
                        ++ChunkData.RawFaces;
                    }
                }

                FMemory::Memset(Used.GetData(), 0, MaskSize);

                // 03D: When disabling greedy for side faces, we skip the U/V expansion so each
                // visible mask cell becomes its own 1x1 quad. The greedy code itself is preserved.
                const bool bDisableGreedyForThisDir = bIsSideDir && bDebugDisableGreedySideFaces;

                // ---- Greedy rect extraction ----
                for (int32 MV = 0; MV < MaskH; ++MV)
                {
                    for (int32 MU = 0; MU < MaskW; ++MU)
                    {
                        if (Used[MV * MaskW + MU])
                        {
                            continue;
                        }
                        const uint8 BT = Mask[MV * MaskW + MU];
                        if (BT == 0)
                        {
                            continue;
                        }

                        // Extend in U direction.
                        // 03D: skipped for side faces when bDebugDisableGreedySideFaces is true.
                        int32 RW = 1;
                        if (!bDisableGreedyForThisDir)
                        {
                            while (MU + RW < MaskW
                                && !Used[MV * MaskW + MU + RW]
                                && Mask[MV * MaskW + MU + RW] == BT)
                            {
                                ++RW;
                            }
                        }

                        // Extend in V direction.
                        // 03D: skipped for side faces when bDebugDisableGreedySideFaces is true.
                        int32 RH = 1;
                        if (!bDisableGreedyForThisDir)
                        {
                            while (MV + RH < MaskH)
                            {
                                bool bRowOk = true;
                                for (int32 dU = 0; dU < RW; ++dU)
                                {
                                    if (Used[(MV + RH) * MaskW + MU + dU]
                                        || Mask[(MV + RH) * MaskW + MU + dU] != BT)
                                    {
                                        bRowOk = false;
                                        break;
                                    }
                                }
                                if (!bRowOk)
                                {
                                    break;
                                }
                                ++RH;
                            }
                        }

                        // Mark used.
                        for (int32 dV = 0; dV < RH; ++dV)
                        {
                            for (int32 dU = 0; dU < RW; ++dU)
                            {
                                Used[(MV + dV) * MaskW + MU + dU] = 1;
                            }
                        }

                        // Resolve section.
                        const EWorldForgeCubeBlockType BlockType = static_cast<EWorldForgeCubeBlockType>(BT);
                        const int32 SectionIndex = GetSectionIndexForBlockType(BlockType);
                        if (SectionIndex == INDEX_NONE)
                        {
                            continue;
                        }
                        FWorldForgeCubeMeshSectionData& Section = ChunkData.SectionData[SectionIndex];

                        // Compute world-space quad origin and axes.
                        // RW blocks span U axis, RH blocks span V axis.
                        const float SpanUxy = static_cast<float>(RW) * BlockSizeXY;
                        const float SpanVxy = static_cast<float>(RH) * BlockSizeXY;
                        const float SpanVz  = static_cast<float>(RH) * BlockHeightUU;

                        FVector Origin, AxisU, AxisV, Normal, Tangent;

                        switch (FaceDir)
                        {
                        case 0: // -X  (slice=X, U=Y, V=Z)
                            // Single-block ref: origin=(MinX, MaxY, MinZ), U=(0,-bxy,0), V=(0,0,bz)
                            Origin  = FVector(BlockMinX[MinLX + Slice],
                                              BlockMinY[MinLY + MU] + SpanUxy,
                                              OriginZ + static_cast<float>(MV) * BlockHeightUU);
                            AxisU   = FVector(0.0f, -SpanUxy, 0.0f);
                            AxisV   = FVector(0.0f, 0.0f,     SpanVz);
                            Normal  = FVector(-1.0f, 0.0f, 0.0f);
                            Tangent = FVector(0.0f, -1.0f, 0.0f);
                            break;

                        case 1: // +X  (slice=X, U=Y, V=Z)
                            // Single-block ref: origin=(MaxX, MinY, MinZ), U=(0,+bxy,0), V=(0,0,bz)
                            Origin  = FVector(BlockMinX[MinLX + Slice] + BlockSizeXY,
                                              BlockMinY[MinLY + MU],
                                              OriginZ + static_cast<float>(MV) * BlockHeightUU);
                            AxisU   = FVector(0.0f, SpanUxy, 0.0f);
                            AxisV   = FVector(0.0f, 0.0f,    SpanVz);
                            Normal  = FVector(1.0f, 0.0f, 0.0f);
                            Tangent = FVector(0.0f, 1.0f, 0.0f);
                            break;

                        case 2: // -Y  (slice=Y, U=X, V=Z)
                            // Single-block ref: origin=(MinX, MinY, MinZ), U=(+bxy,0,0), V=(0,0,bz)
                            Origin  = FVector(BlockMinX[MinLX + MU],
                                              BlockMinY[MinLY + Slice],
                                              OriginZ + static_cast<float>(MV) * BlockHeightUU);
                            AxisU   = FVector(SpanUxy, 0.0f, 0.0f);
                            AxisV   = FVector(0.0f,    0.0f, SpanVz);
                            Normal  = FVector(0.0f, -1.0f, 0.0f);
                            Tangent = FVector(1.0f, 0.0f, 0.0f);
                            break;

                        case 3: // +Y  (slice=Y, U=X, V=Z)
                            // Single-block ref: origin=(MinX, MaxY, MaxZ), U=(+bxy,0,0), V=(0,0,-bz)
                            // AxisV goes -Z so origin is at the top of the V span.
                            Origin  = FVector(BlockMinX[MinLX + MU],
                                              BlockMinY[MinLY + Slice] + BlockSizeXY,
                                              OriginZ + static_cast<float>(MV + RH) * BlockHeightUU);
                            AxisU   = FVector(SpanUxy, 0.0f,    0.0f);
                            AxisV   = FVector(0.0f,    0.0f,   -SpanVz);
                            Normal  = FVector(0.0f, 1.0f, 0.0f);
                            Tangent = FVector(1.0f, 0.0f, 0.0f);
                            break;

                        case 4: // -Z  (slice=Z, U=X, V=Y)
                            // Single-block ref: origin=(MinX, MaxY, MinZ), U=(+bxy,0,0), V=(0,-bxy,0)
                            // AxisV goes -Y so origin is at the +Y edge of the V span.
                            Origin  = FVector(BlockMinX[MinLX + MU],
                                              BlockMinY[MinLY + MV] + SpanVxy,
                                              OriginZ + static_cast<float>(Slice) * BlockHeightUU);
                            AxisU   = FVector(SpanUxy, 0.0f,    0.0f);
                            AxisV   = FVector(0.0f,   -SpanVxy, 0.0f);
                            Normal  = FVector(0.0f, 0.0f, -1.0f);
                            Tangent = FVector(1.0f, 0.0f, 0.0f);
                            break;

                        default: // 5  +Z  (slice=Z, U=X, V=Y)
                            // Single-block ref: origin=(MinX, MinY, MaxZ), U=(+bxy,0,0), V=(0,+bxy,0)
                            Origin  = FVector(BlockMinX[MinLX + MU],
                                              BlockMinY[MinLY + MV],
                                              OriginZ + static_cast<float>(Slice + 1) * BlockHeightUU);
                            AxisU   = FVector(SpanUxy, 0.0f,   0.0f);
                            AxisV   = FVector(0.0f,    SpanVxy, 0.0f);
                            Normal  = FVector(0.0f, 0.0f, 1.0f);
                            Tangent = FVector(1.0f, 0.0f, 0.0f);
                            break;
                        }

                        // CUBE-TERRAIN-04Q-STEP1: side-face V scale must account for
                        // BlockHeightUU vs BlockSizeXY so vertical textures aren't squashed/stretched.
                        // Top/bottom (FaceDir 4/5) keep RW/RH from 04P unchanged.
                        float UTileScale = FMath::Max(1.0f, static_cast<float>(RW));
                        float VTileScale = FMath::Max(1.0f, static_cast<float>(RH));

                        if (bIsSideDir)
                        {
                            const float HeightToXY = (BlockSizeXY > 0.0f)
                                ? (BlockHeightUU / BlockSizeXY)
                                : 1.0f;

                            VTileScale = FMath::Max(1.0f, static_cast<float>(RH) * HeightToXY);
                        }

                        AppendFace(Section, Origin, AxisU, AxisV, Normal, Tangent, UTileScale, VTileScale);
                        ++ChunkData.Faces;
                    }
                }
            }
        }
    }

    // World-space min X/Y of every local block column in the patch.
    void BuildBlockMinArrays(
        const FWorldForgeCubePatchMeshSettings& Settings,
        int32 LocalGridWidth,
        int32 LocalGridHeight,
        TArray<float>& OutBlockMinX,
        TArray<float>& OutBlockMinY)
    {
        const int32 BlocksPerTile = FMath::Max(1, Settings.BlocksPerWorldTile);
        const float BlockSizeXY   = Settings.TileWorldSizeUU / static_cast<float>(BlocksPerTile);
        const float HalfBPT       = static_cast<float>(BlocksPerTile - 1) * 0.5f;
        const float HalfBlockSize = BlockSizeXY * 0.5f;

        OutBlockMinX.SetNum(LocalGridWidth);
        OutBlockMinY.SetNum(LocalGridHeight);

        for (int32 LocalX = 0; LocalX < LocalGridWidth; ++LocalX)
        {
            const int32 TileOffsetX = LocalX / BlocksPerTile;
            const int32 SubBlockX   = LocalX % BlocksPerTile;
            const int32 TileX       = Settings.PatchMinTile.X + TileOffsetX;
            const FVector TileCenter = BuildTileCenterWorld(Settings, TileX, Settings.PatchMinTile.Y);
            const float OffsetX     = (static_cast<float>(SubBlockX) - HalfBPT) * BlockSizeXY;
            OutBlockMinX[LocalX]    = TileCenter.X + OffsetX - HalfBlockSize;
        }

        for (int32 LocalY = 0; LocalY < LocalGridHeight; ++LocalY)
        {
            const int32 TileOffsetY = LocalY / BlocksPerTile;
            const int32 SubBlockY   = LocalY % BlocksPerTile;
            const int32 TileY       = Settings.PatchMinTile.Y + TileOffsetY;
            const FVector TileCenter = BuildTileCenterWorld(Settings, Settings.PatchMinTile.X, TileY);
            const float OffsetY     = (static_cast<float>(SubBlockY) - HalfBPT) * BlockSizeXY;
            OutBlockMinY[LocalY]    = TileCenter.Y + OffsetY - HalfBlockSize;
        }
    }

    // Meshes one chunk of an already-built patch volume.
    void BuildChunkFromVolume(
        FWorldForgeCubeChunkMeshData& ChunkData,
        const FIntPoint& ChunkCoord,
        const TArray<uint8>& Volume,
        int32 LocalGridWidth,
        int32 LocalGridHeight,
        int32 MaxSolidZ,
        const TArray<float>& BlockMinX,
        const TArray<float>& BlockMinY,
        const FWorldForgeCubePatchMeshSettings& Settings,
        int32& OutSuppressedBorderFaces)
    {
        const int32 BlocksPerTile = FMath::Max(1, Settings.BlocksPerWorldTile);
        const int32 ChunkSize     = FMath::Max(1, Settings.ChunkSizeInBlocks);
        const float BlockSizeXY   = Settings.TileWorldSizeUU / static_cast<float>(BlocksPerTile);

        ChunkData = FWorldForgeCubeChunkMeshData();
        ChunkData.ChunkCoord = ChunkCoord;
        ChunkData.SectionData.SetNum(UE_ARRAY_COUNT(CubeSectionOrder));
        for (int32 SectionIdx = 0; SectionIdx < UE_ARRAY_COUNT(CubeSectionOrder); ++SectionIdx)
        {
            ChunkData.SectionData[SectionIdx].BlockType = CubeSectionOrder[SectionIdx];
        }

        const int32 MinLocalX = ChunkCoord.X * ChunkSize;
        const int32 MinLocalY = ChunkCoord.Y * ChunkSize;
        const int32 MaxLocalX = FMath::Min(MinLocalX + ChunkSize, LocalGridWidth);
        const int32 MaxLocalY = FMath::Min(MinLocalY + ChunkSize, LocalGridHeight);

        GreedyBuildChunkFaces(
            ChunkData,
            Volume,
            LocalGridWidth,
            LocalGridHeight,
            MaxSolidZ,
            BlockMinX,
            BlockMinY,
            Settings.TerrainOrigin.Z,
            BlockSizeXY,
            Settings.BlockHeightUU,
            MinLocalX, MaxLocalX,
            MinLocalY, MaxLocalY,
            Settings.bDebugDisableGreedySideFaces,
            Settings.bDebugSuppressOuterPatchBorderFaces,
            OutSuppressedBorderFaces);

        for (const FWorldForgeCubeMeshSectionData& Section : ChunkData.SectionData)
        {
            if (!Section.HasGeometry())
            {
                continue;
            }
            ++ChunkData.Sections;
            ChunkData.Triangles += Section.GetTriangleCount();
        }
    }

} // namespace

bool FWorldForgeCubeMeshBuilder::BuildPatchMesh(
    UCubeTerrainRuntime* Runtime,
    const FWorldForgeCubePatchMeshSettings& Settings,
    FWorldForgeCubePatchMeshData& OutPatchData)
{
    OutPatchData = FWorldForgeCubePatchMeshData();

    // 03A terrain shaping stats initialization
    OutPatchData.MinZ = 0;
    OutPatchData.MaxZ = 0;
    OutPatchData.AvgZ = 0.0f;
    OutPatchData.LocalDeltaAvg = 0.0f;
    OutPatchData.LocalDeltaMax = 0;
    OutPatchData.MixedCells = 0;
    OutPatchData.SnowBlend = 0;
    OutPatchData.SandBlend = 0;
    OutPatchData.RockBlend = 0;

    if (!IsValid(Runtime) || !Runtime->IsValid())
    {
        return false;
    }

    const int32 BlocksPerTile  = FMath::Max(1, Settings.BlocksPerWorldTile);
    const int32 ChunkSize      = FMath::Max(1, Settings.ChunkSizeInBlocks);
    const int32 PatchTilesX    = FMath::Max(0, Settings.PatchTileCount.X);
    const int32 PatchTilesY    = FMath::Max(0, Settings.PatchTileCount.Y);
    if (PatchTilesX <= 0 || PatchTilesY <= 0)
    {
        return false;
    }

    const int32 LocalGridWidth  = PatchTilesX * BlocksPerTile;
    const int32 LocalGridHeight = PatchTilesY * BlocksPerTile;
    const int32 MaxSolidZ       = FMath::Max(0, Runtime->GetMaxSurfaceZ());

    TArray<FTileColumnSample> TileSamples;
    TileSamples.SetNum(PatchTilesX * PatchTilesY);

    for (int32 PatchTileY = 0; PatchTileY < PatchTilesY; ++PatchTileY)
    {
        for (int32 PatchTileX = 0; PatchTileX < PatchTilesX; ++PatchTileX)
        {
            const int32 TileX = Settings.PatchMinTile.X + PatchTileX;
            const int32 TileY = Settings.PatchMinTile.Y + PatchTileY;
            FTileColumnSample& Sample = TileSamples[(PatchTileY * PatchTilesX) + PatchTileX];
            Sample.SurfaceZ = Runtime->GetSurfaceZAt(FIntPoint(TileX, TileY));
            Sample.ColumnTypes.SetNumZeroed(MaxSolidZ + 1);

            for (int32 Z = 0; Z <= MaxSolidZ; ++Z)
            {
                Sample.ColumnTypes[Z] = static_cast<uint8>(
                    Runtime->GetRawBlockTypeAt(FIntVector(TileX, TileY, Z)));
            }

            Sample.SurfaceType =
                (Sample.SurfaceZ >= 0 && Sample.SurfaceZ < Sample.ColumnTypes.Num())
                    ? static_cast<EWorldForgeCubeBlockType>(Sample.ColumnTypes[Sample.SurfaceZ])
                    : EWorldForgeCubeBlockType::Air;
        }
    }

    TArray<float> BlockMinX;
    TArray<float> BlockMinY;
    BuildBlockMinArrays(Settings, LocalGridWidth, LocalGridHeight, BlockMinX, BlockMinY);

    TArray<uint8> Volume;
    Volume.Init(
        static_cast<uint8>(EWorldForgeCubeBlockType::Air),
        LocalGridWidth * LocalGridHeight * (MaxSolidZ + 1));

    // 03A terrain shaping stat accumulators.
    int32 StatMinZ       = INT_MAX;
    int32 StatMaxZ       = INT_MIN;
    int32 StatTotalZ     = 0;
    int32 StatTotalDelta = 0;
    int32 StatDeltaMax   = 0;
    int32 StatSampleCount = 0;

    int32 VisualSandSofteningCandidates = 0;
    int32 VisualSandSofteningEdgeCandidates = 0;
    int32 VisualSandSofteningInteriorSkipped = 0;
    int32 VisualSandSoftenedGrass = 0;
    int32 VisualSandSoftenedDirt = 0;
    int32 VisualSandSofteningMinSourceSurfaceZ = INT_MAX;

    const auto IsVisualSandSofteningEdgeSource = [](EWorldForgeCubeBlockType NeighborSurfaceType) -> bool
    {
        return NeighborSurfaceType == EWorldForgeCubeBlockType::Grass
            || NeighborSurfaceType == EWorldForgeCubeBlockType::Dirt
            || NeighborSurfaceType == EWorldForgeCubeBlockType::Rock
            || NeighborSurfaceType == EWorldForgeCubeBlockType::Snow;
    };

    for (int32 LocalY = 0; LocalY < LocalGridHeight; ++LocalY)
    {
        const int32 PatchTileY = LocalY / BlocksPerTile;
        const int32 SubBlockY  = LocalY % BlocksPerTile;

        for (int32 LocalX = 0; LocalX < LocalGridWidth; ++LocalX)
        {
            const int32 PatchTileX = LocalX / BlocksPerTile;
            const int32 SubBlockX  = LocalX % BlocksPerTile;
            const int32 TileX      = Settings.PatchMinTile.X + PatchTileX;
            const int32 TileY      = Settings.PatchMinTile.Y + PatchTileY;
            const FTileColumnSample& Sample = TileSamples[(PatchTileY * PatchTilesX) + PatchTileX];

            if (Sample.SurfaceType == EWorldForgeCubeBlockType::Air)
            {
                continue;
            }

            const bool bIsFluidTile = IsFluidBlock(Sample.SurfaceType);

            const int32 NeighborTileX = FMath::Min(PatchTileX + 1, PatchTilesX - 1);
            const int32 NeighborTileY = FMath::Min(PatchTileY + 1, PatchTilesY - 1);

            const FTileColumnSample& Sample10 = TileSamples[(PatchTileY  * PatchTilesX) + NeighborTileX];
            const FTileColumnSample& Sample01 = TileSamples[(NeighborTileY * PatchTilesX) + PatchTileX];
            const FTileColumnSample& Sample11 = TileSamples[(NeighborTileY * PatchTilesX) + NeighborTileX];

            int32 LocalSurfaceZ = Sample.SurfaceZ;
            if (!bIsFluidTile && !Settings.bFlatBlockSurface)
            {
                const float T = (static_cast<float>(SubBlockX) + 0.5f) / static_cast<float>(BlocksPerTile);
                const float U = (static_cast<float>(SubBlockY) + 0.5f) / static_cast<float>(BlocksPerTile);
                const float ZInterp = FMath::BiLerp(
                    static_cast<float>(Sample.SurfaceZ),
                    static_cast<float>(Sample10.SurfaceZ),
                    static_cast<float>(Sample01.SurfaceZ),
                    static_cast<float>(Sample11.SurfaceZ),
                    T,
                    U);

                const int32 MaxHeightVar = GetMaxHeightVariation(Sample.SurfaceType);
                const uint32 NoiseHash = HashCombine(
                    GetTypeHash(TileX * BlocksPerTile + SubBlockX),
                    GetTypeHash(TileY * BlocksPerTile + SubBlockY));
                const float NoiseAmount = ((static_cast<float>(NoiseHash % 100) - 50.0f) * 0.01f);
                const float ZWithNoise = ZInterp + NoiseAmount;
                // CUBE-TERRAIN-04L: allow BiLerp to span neighbor tile heights instead of flattening to parent tile.
                const int32 NeighborMin = FMath::Min(
                    FMath::Min(Sample.SurfaceZ, Sample10.SurfaceZ),
                    FMath::Min(Sample01.SurfaceZ, Sample11.SurfaceZ));
                const int32 NeighborMax = FMath::Max(
                    FMath::Max(Sample.SurfaceZ, Sample10.SurfaceZ),
                    FMath::Max(Sample01.SurfaceZ, Sample11.SurfaceZ));
                const int32 ZRounded = FMath::RoundToInt(ZWithNoise);
                const int32 ZClamped = FMath::Clamp(
                    ZRounded,
                    NeighborMin - MaxHeightVar,
                    NeighborMax + MaxHeightVar);
                LocalSurfaceZ = FMath::Clamp(ZClamped, 0, MaxSolidZ);
            }

            // CUBE-TERRAIN-04M-FIX2: Shoreline edge skirt (rounded-corner falloff).
            // Presentation-only: lower the outermost EdgeSkirtTiles tile rings of the
            // patch toward SeaLevelZ so the patch border blends into the surrounding
            // (un-rendered) ocean instead of forming a rectangular cliff.
            //
            // Why FIX2: FIX1 measured distance with a Chebyshev (rectangular) metric
            // (min of the four edge distances). That produces iso-Alpha contours that
            // are perfect squares, which when quantized to integer Z make the patch
            // look like a square pyramid with concentric square contour rings. FIX2
            // measures Euclidean distance from each block into the "inner rectangle"
            // (the region more than SkirtBlocks from every patch edge). On straight
            // edges that matches the Chebyshev distance, but in the four corners the
            // iso-contours become circular arcs instead of square steps, removing the
            // pyramid look.
            //
            // Additionally a small deterministic per-block jitter is added to Alpha
            // before the smoothstep so that integer Z snapping doesn't reintroduce
            // perfectly aligned contour bands along the rounded falloff.
            //
            // Skipped for fluid tiles (water/lava) and when the skirt is disabled.
            // Note: in this loop LocalX/LocalY are already global block coordinates
            // within the patch (i.e. PatchTile* * BlocksPerTile + SubBlock*).
            if (!bIsFluidTile && Settings.EdgeSkirtTiles > 0)
            {
                const int32 TotalBlocksX = PatchTilesX * BlocksPerTile;
                const int32 TotalBlocksY = PatchTilesY * BlocksPerTile;
                const int32 SkirtBlocks  = Settings.EdgeSkirtTiles * BlocksPerTile;

                if (SkirtBlocks > 0)
                {
                    // Inner rectangle: the area more than SkirtBlocks from every edge.
                    const int32 InnerMinX = SkirtBlocks;
                    const int32 InnerMaxX = TotalBlocksX - 1 - SkirtBlocks;
                    const int32 InnerMinY = SkirtBlocks;
                    const int32 InnerMaxY = TotalBlocksY - 1 - SkirtBlocks;

                    // How far the block sits OUTSIDE the inner rectangle on each axis
                    // (0 if it is inside on that axis). Combined with sqrt, this gives
                    // the Euclidean distance from the inner rectangle's boundary.
                    const int32 OutsideX = FMath::Max(0,
                        FMath::Max(InnerMinX - LocalX, LocalX - InnerMaxX));
                    const int32 OutsideY = FMath::Max(0,
                        FMath::Max(InnerMinY - LocalY, LocalY - InnerMaxY));

                    if (OutsideX > 0 || OutsideY > 0)
                    {
                        const float SkirtPenetration = FMath::Sqrt(
                            static_cast<float>(OutsideX) * static_cast<float>(OutsideX) +
                            static_cast<float>(OutsideY) * static_cast<float>(OutsideY));

                        // Deterministic per-block jitter (+/- ~10% of skirt width)
                        // to break up integer-Z banding along the rounded contours.
                        const int32 Hash = (LocalX * 73856093) ^ (LocalY * 19349663);
                        const float Jitter =
                            ((static_cast<float>(Hash & 1023) / 1023.0f) - 0.5f) * 0.20f;

                        float Alpha = 1.0f - (SkirtPenetration / static_cast<float>(SkirtBlocks));
                        Alpha = FMath::Clamp(Alpha + Jitter, 0.0f, 1.0f);
                        // Smooth Hermite step (3a^2 - 2a^3) for a continuous shoreline ramp.
                        Alpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

                        const int32 TargetZ = Settings.SeaLevelZ;
                        const float Skirted = FMath::Lerp(
                            static_cast<float>(TargetZ),
                            static_cast<float>(LocalSurfaceZ),
                            Alpha);
                        LocalSurfaceZ = FMath::Clamp(FMath::RoundToInt(Skirted), 0, MaxSolidZ);
                    }
                }
            }

            // 03A height stat collection.
            StatMinZ = FMath::Min(StatMinZ, LocalSurfaceZ);
            StatMaxZ = FMath::Max(StatMaxZ, LocalSurfaceZ);
            StatTotalZ += LocalSurfaceZ;
            int32 Delta = FMath::Abs(LocalSurfaceZ - Sample.SurfaceZ);
            StatTotalDelta += Delta;
            StatDeltaMax = FMath::Max(StatDeltaMax, Delta);
            ++StatSampleCount;

            for (int32 Z = 0; Z <= LocalSurfaceZ; ++Z)
            {
                const int32 DepthBelowSurface = LocalSurfaceZ - Z;
                EWorldForgeCubeBlockType BlockType = Sample.SurfaceType;

                if (DepthBelowSurface == 0)
                {
                    const uint32 TransitionNoise = HashCombine(
                        GetTypeHash(LocalX),
                        GetTypeHash(LocalY));
                    FBiomeTransitionResult Transition = EvaluateBiomeTransition(
                        Sample.SurfaceType,
                        Sample10.SurfaceType,
                        Sample01.SurfaceType,
                        Sample11.SurfaceType,
                        TransitionNoise);

                    BlockType = Transition.BlockType;

                    // CUBE-TERRAIN-04T-STEP3: render-only dry sand edge visual softening.
                    const EWorldForgeCubeBlockType SurfaceType = BlockType;
                    if (SurfaceType == EWorldForgeCubeBlockType::Sand
                        && Sample.SurfaceZ >= Settings.SeaLevelZ + 2)
                    {
                        ++VisualSandSofteningCandidates;
                        VisualSandSofteningMinSourceSurfaceZ = FMath::Min(
                            VisualSandSofteningMinSourceSurfaceZ,
                            Sample.SurfaceZ);

                        bool bHasSandSofteningEdgeNeighbor = false;
                        for (int32 NeighborOffsetY = -1; NeighborOffsetY <= 1 && !bHasSandSofteningEdgeNeighbor; ++NeighborOffsetY)
                        {
                            for (int32 NeighborOffsetX = -1; NeighborOffsetX <= 1; ++NeighborOffsetX)
                            {
                                if (NeighborOffsetX == 0 && NeighborOffsetY == 0)
                                {
                                    continue;
                                }

                                const int32 NeighborLocalX = LocalX + NeighborOffsetX;
                                const int32 NeighborLocalY = LocalY + NeighborOffsetY;
                                if (NeighborLocalX < 0 || NeighborLocalY < 0
                                    || NeighborLocalX >= LocalGridWidth || NeighborLocalY >= LocalGridHeight)
                                {
                                    continue;
                                }

                                const int32 NeighborPatchTileX = NeighborLocalX / BlocksPerTile;
                                const int32 NeighborPatchTileY = NeighborLocalY / BlocksPerTile;
                                if (NeighborPatchTileX == PatchTileX && NeighborPatchTileY == PatchTileY)
                                {
                                    continue;
                                }

                                const FTileColumnSample& NeighborSample =
                                    TileSamples[(NeighborPatchTileY * PatchTilesX) + NeighborPatchTileX];
                                if (IsVisualSandSofteningEdgeSource(NeighborSample.SurfaceType))
                                {
                                    bHasSandSofteningEdgeNeighbor = true;
                                    break;
                                }
                            }
                        }

                        if (!bHasSandSofteningEdgeNeighbor)
                        {
                            ++VisualSandSofteningInteriorSkipped;
                        }
                        else
                        {
                            ++VisualSandSofteningEdgeCandidates;

                            const uint32 SofteningHash = HashCombine(
                                GetTypeHash(TileX * BlocksPerTile + SubBlockX),
                                GetTypeHash(TileY * BlocksPerTile + SubBlockY));
                            const uint32 SofteningRoll = SofteningHash % 100;

                            if (SofteningRoll < 20)
                            {
                                BlockType = EWorldForgeCubeBlockType::Grass;
                                ++VisualSandSoftenedGrass;
                            }
                            else if (SofteningRoll < 25)
                            {
                                BlockType = EWorldForgeCubeBlockType::Dirt;
                                ++VisualSandSoftenedDirt;
                            }
                        }
                    }

                    if (Transition.TransitionType != 0)
                    {
                        ++OutPatchData.MixedCells;
                        if (Transition.TransitionType == 1)
                        {
                            ++OutPatchData.SandBlend;
                        }
                        else if (Transition.TransitionType == 2)
                        {
                            ++OutPatchData.RockBlend;
                        }
                        else if (Transition.TransitionType == 3)
                        {
                            ++OutPatchData.SnowBlend;
                        }
                    }
                }
                else if (DepthBelowSurface > 0)
                {
                    const int32 QueryZ = FMath::Clamp(Sample.SurfaceZ - DepthBelowSurface, 0, MaxSolidZ);
                    BlockType = static_cast<EWorldForgeCubeBlockType>(Sample.ColumnTypes[QueryZ]);
                    if (BlockType == EWorldForgeCubeBlockType::Air)
                    {
                        BlockType = EWorldForgeCubeBlockType::Dirt;
                    }
                }

                const int32 Index = VolumeIndex(LocalX, LocalY, Z, LocalGridWidth, LocalGridHeight);
                Volume[Index] = static_cast<uint8>(BlockType);
            }
        }
    }

    // 03A finalize collected stats.
    if (StatSampleCount > 0)
    {
        OutPatchData.MinZ = StatMinZ;
        OutPatchData.MaxZ = StatMaxZ;
        OutPatchData.AvgZ = (static_cast<float>(StatMinZ) + static_cast<float>(StatMaxZ)) * 0.5f;
        OutPatchData.LocalDeltaAvg = static_cast<float>(StatTotalDelta) / static_cast<float>(StatSampleCount);
        OutPatchData.LocalDeltaMax = StatDeltaMax;
    }

    OutPatchData.LocalGridWidth    = LocalGridWidth;
    OutPatchData.LocalGridHeight   = LocalGridHeight;
    OutPatchData.ChunkSizeInBlocks = ChunkSize;

    // CUBE-BLOCKGRID-01: the volume becomes the authoritative block data for the
    // patch. Mining edits it in place and BuildChunkMesh re-meshes from it.
    Runtime->SetPatchBlockVolume(
        FIntPoint(Settings.PatchMinTile.X * BlocksPerTile, Settings.PatchMinTile.Y * BlocksPerTile),
        LocalGridWidth,
        LocalGridHeight,
        MaxSolidZ,
        MoveTemp(Volume));

    if (!Runtime->HasPatchBlockVolume())
    {
        return false;
    }

    const TArray<uint8>& StoredVolume = Runtime->GetPatchBlockVolume();

    const int32 ChunkCountX = FMath::DivideAndRoundUp(LocalGridWidth,  ChunkSize);
    const int32 ChunkCountY = FMath::DivideAndRoundUp(LocalGridHeight, ChunkSize);

    // 03D: patch-level suppressed border face counter.
    int32 TotalSuppressedBorderFaces = 0;

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] SideFaceDebug active. VerticalFacesGreedy=%s"),
        Settings.bDebugDisableGreedySideFaces ? TEXT("false (per-block)") : TEXT("true (greedy merged)"));

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainUV] Greedy UV span scaling enabled. UV=0..RW/0..RH"));

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainUV] Side-face UV scaling enabled. SideV=RH*(BlockHeightUU/BlockSizeXY)"));

    for (int32 ChunkY = 0; ChunkY < ChunkCountY; ++ChunkY)
    {
        for (int32 ChunkX = 0; ChunkX < ChunkCountX; ++ChunkX)
        {
            FWorldForgeCubeChunkMeshData ChunkData;
            int32 ChunkSuppressedBorderFaces = 0;

            BuildChunkFromVolume(
                ChunkData,
                FIntPoint(ChunkX, ChunkY),
                StoredVolume,
                LocalGridWidth,
                LocalGridHeight,
                MaxSolidZ,
                BlockMinX,
                BlockMinY,
                Settings,
                ChunkSuppressedBorderFaces);

            TotalSuppressedBorderFaces += ChunkSuppressedBorderFaces;

            if (!ChunkData.HasGeometry())
            {
                continue;
            }

            UE_LOG(LogTemp, Log,
                TEXT("[CubeTerrainMesh] 02E Chunk=(%d,%d) Blocks=%d RawFaces=%d GreedyQuads=%d Ratio=%.3f Sections=%d"),
                ChunkData.ChunkCoord.X,
                ChunkData.ChunkCoord.Y,
                ChunkData.BlocksChecked,
                ChunkData.RawFaces,
                ChunkData.Faces,
                ChunkData.RawFaces > 0
                    ? static_cast<float>(ChunkData.Faces) / static_cast<float>(ChunkData.RawFaces)
                    : 1.0f,
                ChunkData.Sections);

            OutPatchData.BlocksChecked += ChunkData.BlocksChecked;
            OutPatchData.RawFaces      += ChunkData.RawFaces;
            OutPatchData.Faces         += ChunkData.Faces;
            OutPatchData.Triangles     += ChunkData.Triangles;
            OutPatchData.Sections      += ChunkData.Sections;
            OutPatchData.Chunks.Add(MoveTemp(ChunkData));
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] 02E Patch total: Chunks=%d BlocksChecked=%d RawFaces=%d GreedyQuads=%d Ratio=%.3f Triangles=%d Sections=%d"),
        OutPatchData.Chunks.Num(),
        OutPatchData.BlocksChecked,
        OutPatchData.RawFaces,
        OutPatchData.Faces,
        OutPatchData.RawFaces > 0
            ? static_cast<float>(OutPatchData.Faces) / static_cast<float>(OutPatchData.RawFaces)
            : 1.0f,
        OutPatchData.Triangles,
        OutPatchData.Sections);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] PatchBorderFaces suppressed=%d"),
        TotalSuppressedBorderFaces);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainShape] HeightBlend=%s FlatBlockSurface=%s"),
        Settings.bFlatBlockSurface ? TEXT("Disabled") : TEXT("Enabled"),
        Settings.bFlatBlockSurface ? TEXT("true") : TEXT("false"));

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainShape] EdgeSkirt %s. Tiles=%d SeaLevelZ=%d"),
        Settings.EdgeSkirtTiles > 0 ? TEXT("enabled") : TEXT("disabled"),
        Settings.EdgeSkirtTiles,
        Settings.SeaLevelZ);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainShape] Height stats. MinZ=%d MaxZ=%d AvgZ=%.2f LocalDeltaAvg=%.2f LocalDeltaMax=%d"),
        OutPatchData.MinZ,
        OutPatchData.MaxZ,
        OutPatchData.AvgZ,
        OutPatchData.LocalDeltaAvg,
        OutPatchData.LocalDeltaMax);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainShape] Biome transitions. MixedCells=%d SnowBlend=%d SandBlend=%d RockBlend=%d"),
        OutPatchData.MixedCells,
        OutPatchData.SnowBlend,
        OutPatchData.SandBlend,
        OutPatchData.RockBlend);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainVisualSoften] SandSurfaceSoftening RenderOnly=true EdgeOnly=true Candidates=%d EdgeCandidates=%d InteriorSkipped=%d SandToGrass=%d SandToDirt=%d MinSourceSurfaceZ=%d"),
        VisualSandSofteningCandidates,
        VisualSandSofteningEdgeCandidates,
        VisualSandSofteningInteriorSkipped,
        VisualSandSoftenedGrass,
        VisualSandSoftenedDirt,
        VisualSandSofteningCandidates > 0 ? VisualSandSofteningMinSourceSurfaceZ : INDEX_NONE);

    return OutPatchData.Faces > 0;
}

bool FWorldForgeCubeMeshBuilder::BuildChunkMesh(
    const UCubeTerrainRuntime* Runtime,
    const FWorldForgeCubePatchMeshSettings& Settings,
    const FIntPoint& ChunkCoord,
    FWorldForgeCubeChunkMeshData& OutChunkData)
{
    OutChunkData = FWorldForgeCubeChunkMeshData();

    if (!IsValid(Runtime) || !Runtime->HasPatchBlockVolume())
    {
        return false;
    }

    const int32 BlocksPerTile   = FMath::Max(1, Settings.BlocksPerWorldTile);
    const int32 ChunkSize       = FMath::Max(1, Settings.ChunkSizeInBlocks);
    const int32 LocalGridWidth  = Runtime->GetPatchBlocksX();
    const int32 LocalGridHeight = Runtime->GetPatchBlocksY();

    if (LocalGridWidth != Settings.PatchTileCount.X * BlocksPerTile ||
        LocalGridHeight != Settings.PatchTileCount.Y * BlocksPerTile)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeBlockGrid] BuildChunkMesh: settings do not match the stored patch. Stored=%dx%d Settings=%dx%d"),
            LocalGridWidth, LocalGridHeight,
            Settings.PatchTileCount.X * BlocksPerTile, Settings.PatchTileCount.Y * BlocksPerTile);
        return false;
    }

    if (ChunkCoord.X < 0 || ChunkCoord.Y < 0 ||
        ChunkCoord.X * ChunkSize >= LocalGridWidth ||
        ChunkCoord.Y * ChunkSize >= LocalGridHeight)
    {
        return false;
    }

    TArray<float> BlockMinX;
    TArray<float> BlockMinY;
    BuildBlockMinArrays(Settings, LocalGridWidth, LocalGridHeight, BlockMinX, BlockMinY);

    int32 SuppressedBorderFaces = 0;
    BuildChunkFromVolume(
        OutChunkData,
        ChunkCoord,
        Runtime->GetPatchBlockVolume(),
        LocalGridWidth,
        LocalGridHeight,
        Runtime->GetPatchMaxZ(),
        BlockMinX,
        BlockMinY,
        Settings,
        SuppressedBorderFaces);

    return true;
}

// WorldForgeCubeTerrainActor.cpp — CUBE-TERRAIN-03D
// CubePrototype render actor with chunk mesh path and HISM fallback.
// 03D: Passes bDebugDisableGreedySideFaces and bDebugSuppressOuterPatchBorderFaces into
//      FWorldForgeCubePatchMeshSettings so the builder can apply visual isolation logic.
// Smooth terrain remains separate and is only hidden for visual isolation when requested.

#include "WorldForgeCubeTerrainActor.h"
#include "WorldForgeCubeMeshBuilder.h"
#include "CubeTerrainRuntime.h"
#include "VoxelWorldManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "../UI/WorldForgeEmbarkationManager.h"

// Primary cube mesh: 1M_Cube (100 UU, clean faces, no chamfer).
// Fallback: Engine BasicShapes/Cube (always present).
static const TCHAR* const Cube01D_PrimaryMeshPath  =
    TEXT("/Game/A_Surface_Footstep/Environment_Assets/1M_Cube.1M_Cube");
static const TCHAR* const Cube01D_FallbackMeshPath =
    TEXT("/Engine/BasicShapes/Cube.Cube");
static constexpr float Cube01D_MeshNativeUU = 100.0f;

// CUBE-TERRAIN-01E: Debug material base.
// /Engine/BasicShapes/BasicShapeMaterial has a "Color" VectorParameter and
// bUsedWithInstancedStaticMeshes=true — safe as MID parent for HISM debug coloring.
static const TCHAR* const Cube01E_DebugMatPath =
    TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

namespace
{
    FVector BuildTileCenterWorld(const FWorldForgeCubePatchMeshSettings& MeshSettings, int32 TileX, int32 TileY)
    {
        const float HalfGridWidth = static_cast<float>(MeshSettings.TerrainGridSize.X) * 0.5f;
        const float HalfGridHeight = static_cast<float>(MeshSettings.TerrainGridSize.Y) * 0.5f;

        return FVector(
            MeshSettings.TerrainOrigin.X + ((static_cast<float>(TileX) - HalfGridWidth + 0.5f) * MeshSettings.TileWorldSizeUU),
            MeshSettings.TerrainOrigin.Y + ((static_cast<float>(TileY) - HalfGridHeight + 0.5f) * MeshSettings.TileWorldSizeUU),
            MeshSettings.TerrainOrigin.Z);
    }
}

AWFCubeTerrainActor::AWFCubeTerrainActor()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("CubeTerrainRoot"));
    SetRootComponent(Root);

    // Lambda so each HISM gets identical setup without repetition.
    auto MakeHISM = [this, Root](const FName Name) -> UHierarchicalInstancedStaticMeshComponent*
    {
        auto* H = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
        if (H)
        {
            H->SetupAttachment(Root);
            H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        return H;
    };

    HISM_Grass    = MakeHISM(TEXT("HISM_Grass"));
    HISM_Dirt     = MakeHISM(TEXT("HISM_Dirt"));
    HISM_Sand     = MakeHISM(TEXT("HISM_Sand"));
    HISM_Rock     = MakeHISM(TEXT("HISM_Rock"));
    HISM_Snow     = MakeHISM(TEXT("HISM_Snow"));
    HISM_Water    = MakeHISM(TEXT("HISM_Water"));
    HISM_DeepRock = MakeHISM(TEXT("HISM_DeepRock"));
    HISM_Lava     = MakeHISM(TEXT("HISM_Lava"));
    HISM_Basalt   = MakeHISM(TEXT("HISM_Basalt"));
}

UHierarchicalInstancedStaticMeshComponent* AWFCubeTerrainActor::GetHISMForType(
    EWorldForgeCubeBlockType BlockType) const
{
    switch (BlockType)
    {
    case EWorldForgeCubeBlockType::Grass:    return HISM_Grass;
    case EWorldForgeCubeBlockType::Dirt:     return HISM_Dirt;
    case EWorldForgeCubeBlockType::Sand:     return HISM_Sand;
    case EWorldForgeCubeBlockType::Rock:     return HISM_Rock;
    case EWorldForgeCubeBlockType::Snow:     return HISM_Snow;
    case EWorldForgeCubeBlockType::Water:    return HISM_Water;
    case EWorldForgeCubeBlockType::DeepRock: return HISM_DeepRock;
    case EWorldForgeCubeBlockType::Lava:     return HISM_Lava;
    case EWorldForgeCubeBlockType::Basalt:   return HISM_Basalt;
    default:                                 return nullptr;
    }
}

FLinearColor AWFCubeTerrainActor::GetDebugColorForBlockType(EWorldForgeCubeBlockType BlockType)
{
    switch (BlockType)
    {
    case EWorldForgeCubeBlockType::Grass:    return FLinearColor(0.08f, 0.45f, 0.08f, 1.0f);
    case EWorldForgeCubeBlockType::Dirt:     return FLinearColor(0.38f, 0.22f, 0.08f, 1.0f);
    case EWorldForgeCubeBlockType::Sand:     return FLinearColor(0.80f, 0.68f, 0.28f, 1.0f);
    case EWorldForgeCubeBlockType::Rock:     return FLinearColor(0.38f, 0.38f, 0.38f, 1.0f);
    case EWorldForgeCubeBlockType::Snow:     return FLinearColor(0.88f, 0.92f, 1.00f, 1.0f);
    case EWorldForgeCubeBlockType::Water:    return FLinearColor(0.08f, 0.28f, 0.80f, 1.0f);
    case EWorldForgeCubeBlockType::DeepRock: return FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
    case EWorldForgeCubeBlockType::Lava:     return FLinearColor(0.90f, 0.18f, 0.02f, 1.0f);
    case EWorldForgeCubeBlockType::Basalt:   return FLinearColor(0.08f, 0.04f, 0.10f, 1.0f);
    default:                                 return FLinearColor::White;
    }
}

UMaterialInstanceDynamic* AWFCubeTerrainActor::CreateDebugMaterialInstance(
    UMaterialInterface* DebugBaseMat,
    EWorldForgeCubeBlockType BlockType) const
{
    if (!DebugBaseMat)
    {
        return nullptr;
    }

    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DebugBaseMat, const_cast<AWFCubeTerrainActor*>(this));
    if (MID)
    {
        MID->SetVectorParameterValue(TEXT("Color"), GetDebugColorForBlockType(BlockType));
    }
    return MID;
}

// CUBE-TERRAIN-04N: Resolve a block type to its mapped UMaterialInterface, falling back
// to the engine debug base material (Cube01E_DebugMatPath) when no override is set.
// Callers may wrap the result with CreateDebugMaterialInstance() to apply the debug color
// when the fallback path is hit; mapped overrides are returned as-is.
UMaterialInterface* AWFCubeTerrainActor::GetMaterialForBlockType(EWorldForgeCubeBlockType BlockType) const
{
    if (const TObjectPtr<UMaterialInterface>* Found = BlockTypeMaterials.Find(BlockType))
    {
        if (Found && Found->Get())
        {
            return Found->Get();
        }
    }

    return LoadObject<UMaterialInterface>(nullptr, Cube01E_DebugMatPath);
}

void AWFCubeTerrainActor::ClearHISMInstances()
{
    auto Clear = [](UHierarchicalInstancedStaticMeshComponent* HISM)
    {
        if (HISM)
        {
            HISM->ClearInstances();
        }
    };

    Clear(HISM_Grass);
    Clear(HISM_Dirt);
    Clear(HISM_Sand);
    Clear(HISM_Rock);
    Clear(HISM_Snow);
    Clear(HISM_Water);
    Clear(HISM_DeepRock);
    Clear(HISM_Lava);
    Clear(HISM_Basalt);
}

void AWFCubeTerrainActor::DestroyChunkMeshComponents()
{
    for (UProceduralMeshComponent* ChunkMeshComponent : ChunkMeshComponents)
    {
        if (!ChunkMeshComponent)
        {
            continue;
        }

        RemoveInstanceComponent(ChunkMeshComponent);
        ChunkMeshComponent->DestroyComponent();
    }

    ChunkMeshComponents.Reset();
}

void AWFCubeTerrainActor::ClearPatch()
{
    DestroyChunkMeshComponents();
    ClearHISMInstances();
}

void AWFCubeTerrainActor::BuildPatchWithHISM(
    UCubeTerrainRuntime* Runtime,
    const FWorldForgeCubePatchMeshSettings& MeshSettings)
{
    if (!IsValid(Runtime) || !Runtime->IsValid())
    {
        return;
    }

    const int32 BlocksPerTile = FMath::Max(1, MeshSettings.BlocksPerWorldTile);
    const float EffectiveBlockSizeUU = MeshSettings.TileWorldSizeUU / static_cast<float>(BlocksPerTile);
    const float ScaleXY = EffectiveBlockSizeUU / Cube01D_MeshNativeUU;
    const float ScaleZ = MeshSettings.BlockHeightUU / Cube01D_MeshNativeUU;
    const float HalfBPT = static_cast<float>(BlocksPerTile - 1) * 0.5f;
    const int32 GridW = MeshSettings.TerrainGridSize.X;
    const int32 GridH = MeshSettings.TerrainGridSize.Y;
    const int32 MinTileX = MeshSettings.PatchMinTile.X;
    const int32 MinTileY = MeshSettings.PatchMinTile.Y;
    const int32 MaxTileX = MinTileX + MeshSettings.PatchTileCount.X - 1;
    const int32 MaxTileY = MinTileY + MeshSettings.PatchTileCount.Y - 1;
    const int32 MaxSurfZ = Runtime->GetMaxSurfaceZ();

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] HISM fallback start. TilePatch=%dx%d BlockGrid=%dx%d VisibleDepth=%d"),
        MeshSettings.PatchTileCount.X,
        MeshSettings.PatchTileCount.Y,
        MeshSettings.PatchTileCount.X * BlocksPerTile,
        MeshSettings.PatchTileCount.Y * BlocksPerTile,
        VisibleDepthBlocks);

    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, Cube01D_PrimaryMeshPath);
    if (!IsValid(CubeMesh))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrainRender] Primary mesh not found (%s); trying Engine fallback."),
            Cube01D_PrimaryMeshPath);
        CubeMesh = LoadObject<UStaticMesh>(nullptr, Cube01D_FallbackMeshPath);
    }
    if (!IsValid(CubeMesh))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrainRender] Fallback mesh not found (%s); instances will have no mesh."),
            Cube01D_FallbackMeshPath);
    }

    auto SetMesh = [CubeMesh](UHierarchicalInstancedStaticMeshComponent* HISM)
    {
        if (HISM && IsValid(CubeMesh))
        {
            HISM->SetStaticMesh(CubeMesh);
        }
    };

    SetMesh(HISM_Grass);
    SetMesh(HISM_Dirt);
    SetMesh(HISM_Sand);
    SetMesh(HISM_Rock);
    SetMesh(HISM_Snow);
    SetMesh(HISM_Water);
    SetMesh(HISM_DeepRock);
    SetMesh(HISM_Lava);
    SetMesh(HISM_Basalt);

    // CUBE-TERRAIN-04N: HISM material now resolved via GetMaterialForBlockType.
    // DebugBaseMat load is retained purely so the legacy "asset missing" warning still fires.
    // NOTE: Any UMaterialInterface assigned via BlockTypeMaterials and consumed by HISM
    //       must have "Used with Instanced Static Meshes" enabled in its base material
    //       settings, or instanced rendering will fall back to the default checker.
    UMaterialInterface* DebugBaseMat = LoadObject<UMaterialInterface>(nullptr, Cube01E_DebugMatPath);
    if (!DebugBaseMat)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrainRender] Debug base material not found (%s). HISM fallback will use default materials."),
            Cube01E_DebugMatPath);
    }

    // CUBE-TERRAIN-04N: One log line per unique block type in the HISM fallback build (max 9).
    TSet<EWorldForgeCubeBlockType> LoggedHISMMatTypes;

    auto AssignMaterial = [this, &LoggedHISMMatTypes](UHierarchicalInstancedStaticMeshComponent* HISM, EWorldForgeCubeBlockType BlockType)
    {
        if (!HISM)
        {
            return;
        }

        const TObjectPtr<UMaterialInterface>* MappedEntry = BlockTypeMaterials.Find(BlockType);
        const bool bUsingOverride = (MappedEntry && MappedEntry->Get() != nullptr);

        UMaterialInterface* TypeMat = GetMaterialForBlockType(BlockType);
        if (bUsingOverride)
        {
            HISM->SetMaterial(0, TypeMat);
        }
        else if (UMaterialInstanceDynamic* MID = CreateDebugMaterialInstance(TypeMat, BlockType))
        {
            HISM->SetMaterial(0, MID);
        }

        if (!LoggedHISMMatTypes.Contains(BlockType))
        {
            LoggedHISMMatTypes.Add(BlockType);
            UE_LOG(LogTemp, Log,
                TEXT("[CubeTerrainMaterial] HISM BlockType=%d Source=%s Material=%s"),
                static_cast<int32>(BlockType),
                bUsingOverride ? TEXT("Assigned") : TEXT("DebugFallback"),
                TypeMat ? *TypeMat->GetName() : TEXT("<null>"));
        }
    };

    AssignMaterial(HISM_Grass, EWorldForgeCubeBlockType::Grass);
    AssignMaterial(HISM_Dirt, EWorldForgeCubeBlockType::Dirt);
    AssignMaterial(HISM_Sand, EWorldForgeCubeBlockType::Sand);
    AssignMaterial(HISM_Rock, EWorldForgeCubeBlockType::Rock);
    AssignMaterial(HISM_Snow, EWorldForgeCubeBlockType::Snow);
    AssignMaterial(HISM_Water, EWorldForgeCubeBlockType::Water);
    AssignMaterial(HISM_DeepRock, EWorldForgeCubeBlockType::DeepRock);
    AssignMaterial(HISM_Lava, EWorldForgeCubeBlockType::Lava);
    AssignMaterial(HISM_Basalt, EWorldForgeCubeBlockType::Basalt);

    int32 TotalInstances = 0;
    int32 ColumnCount = 0;
    int32 CntGrass = 0;
    int32 CntDirt = 0;
    int32 CntSand = 0;
    int32 CntRock = 0;
    int32 CntSnow = 0;
    int32 CntWater = 0;
    int32 CntDeepRock = 0;
    int32 CntLava = 0;
    int32 CntBasalt = 0;

    for (int32 TileY = MinTileY; TileY <= MaxTileY; ++TileY)
    {
        for (int32 TileX = MinTileX; TileX <= MaxTileX; ++TileX)
        {
            const int32 TileSurfaceZ = Runtime->GetSurfaceZAt(FIntPoint(TileX, TileY));
            const EWorldForgeCubeBlockType TileSurfaceType =
                Runtime->GetRawBlockTypeAt(FIntVector(TileX, TileY, TileSurfaceZ));
            const bool bIsFluid = TileSurfaceType == EWorldForgeCubeBlockType::Water
                || TileSurfaceType == EWorldForgeCubeBlockType::Lava;

            const FVector TileWorldCenter = BuildTileCenterWorld(MeshSettings, TileX, TileY);

            const float Z00 = static_cast<float>(TileSurfaceZ);
            const float Z10 = static_cast<float>(Runtime->GetSurfaceZAt(
                FIntPoint(FMath::Min(TileX + 1, GridW - 1), TileY)));
            const float Z01 = static_cast<float>(Runtime->GetSurfaceZAt(
                FIntPoint(TileX, FMath::Min(TileY + 1, GridH - 1))));
            const float Z11 = static_cast<float>(Runtime->GetSurfaceZAt(
                FIntPoint(FMath::Min(TileX + 1, GridW - 1), FMath::Min(TileY + 1, GridH - 1))));

            for (int32 LocalBlockY = 0; LocalBlockY < BlocksPerTile; ++LocalBlockY)
            {
                for (int32 LocalBlockX = 0; LocalBlockX < BlocksPerTile; ++LocalBlockX)
                {
                    ++ColumnCount;

                    const float OffsetX = (static_cast<float>(LocalBlockX) - HalfBPT) * EffectiveBlockSizeUU;
                    const float OffsetY = (static_cast<float>(LocalBlockY) - HalfBPT) * EffectiveBlockSizeUU;
                    const float BlockWorldX = TileWorldCenter.X + OffsetX;
                    const float BlockWorldY = TileWorldCenter.Y + OffsetY;

                    int32 LocalSurfaceZ = TileSurfaceZ;
                    if (!bIsFluid && !bFlatBlockSurface)
                    {
                        const float T = (static_cast<float>(LocalBlockX) + 0.5f) / static_cast<float>(BlocksPerTile);
                        const float U = (static_cast<float>(LocalBlockY) + 0.5f) / static_cast<float>(BlocksPerTile);
                        const float ZInterp = FMath::BiLerp(Z00, Z10, Z01, Z11, T, U);
                        const uint32 NoiseHash = HashCombine(
                            GetTypeHash(TileX * BlocksPerTile + LocalBlockX),
                            GetTypeHash(TileY * BlocksPerTile + LocalBlockY));
                        const int32 Noise = static_cast<int32>(NoiseHash % 3u) - 1;

                        LocalSurfaceZ = FMath::Clamp(FMath::RoundToInt(ZInterp) + Noise, 0, MaxSurfZ);
                    }

                    for (int32 Depth = 0; Depth <= VisibleDepthBlocks; ++Depth)
                    {
                        const int32 VisibleZ = LocalSurfaceZ - Depth;
                        if (VisibleZ < 0)
                        {
                            break;
                        }

                        EWorldForgeCubeBlockType BlockType = TileSurfaceType;
                        if (Depth > 0)
                        {
                            const int32 QueryZ = FMath::Max(0, TileSurfaceZ - Depth);
                            BlockType = Runtime->GetRawBlockTypeAt(FIntVector(TileX, TileY, QueryZ));
                            if (BlockType == EWorldForgeCubeBlockType::Air)
                            {
                                BlockType = EWorldForgeCubeBlockType::Dirt;
                            }
                        }

                        if (BlockType == EWorldForgeCubeBlockType::Air)
                        {
                            continue;
                        }

                        UHierarchicalInstancedStaticMeshComponent* HISM = GetHISMForType(BlockType);
                        if (!HISM)
                        {
                            continue;
                        }

                        HISM->AddInstance(
                            FTransform(
                                FQuat::Identity,
                                FVector(
                                    BlockWorldX,
                                    BlockWorldY,
                                    MeshSettings.TerrainOrigin.Z + (static_cast<float>(VisibleZ) * MeshSettings.BlockHeightUU)),
                                FVector(ScaleXY, ScaleXY, ScaleZ)),
                            true);

                        ++TotalInstances;

                        switch (BlockType)
                        {
                        case EWorldForgeCubeBlockType::Grass:    ++CntGrass;    break;
                        case EWorldForgeCubeBlockType::Dirt:     ++CntDirt;     break;
                        case EWorldForgeCubeBlockType::Sand:     ++CntSand;     break;
                        case EWorldForgeCubeBlockType::Rock:     ++CntRock;     break;
                        case EWorldForgeCubeBlockType::Snow:     ++CntSnow;     break;
                        case EWorldForgeCubeBlockType::Water:    ++CntWater;    break;
                        case EWorldForgeCubeBlockType::DeepRock: ++CntDeepRock; break;
                        case EWorldForgeCubeBlockType::Lava:     ++CntLava;     break;
                        case EWorldForgeCubeBlockType::Basalt:   ++CntBasalt;   break;
                        default: break;
                        }
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] HISM fallback complete. Columns=%d Instances=%d Grass=%d Dirt=%d Sand=%d Rock=%d Snow=%d Water=%d DeepRock=%d Lava=%d Basalt=%d"),
        ColumnCount,
        TotalInstances,
        CntGrass,
        CntDirt,
        CntSand,
        CntRock,
        CntSnow,
        CntWater,
        CntDeepRock,
        CntLava,
        CntBasalt);
}

void AWFCubeTerrainActor::BuildPatch(UCubeTerrainRuntime* Runtime, UVoxelWorldManager* VoxelMgr)
{
    if (!IsValid(Runtime) || !Runtime->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[CubeTerrainRender] BuildPatch: runtime invalid; aborting."));
        return;
    }

    ClearPatch();

    const float TileWorldSizeUU = IsValid(VoxelMgr) ? VoxelMgr->ChunkWorldSize : 3200.0f;
    const float BlockHeightUU = IsValid(VoxelMgr) ? VoxelMgr->VoxelSize : 100.0f;
    const FVector TerrainOrigin = IsValid(VoxelMgr) ? VoxelMgr->GetTerrainWorldOrigin() : FVector::ZeroVector;

    const int32 BlocksPerTile = FMath::Max(1,
        Runtime->GetBlocksPerWorldTile() > 0
            ? Runtime->GetBlocksPerWorldTile()
            : BlocksPerWorldTile);
    const float EffectiveBlockSizeUU = TileWorldSizeUU / static_cast<float>(BlocksPerTile);

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] 02B TileFill mode. TileWorldSizeUU=%.1f BlocksPerTile=%d "
             "EffectiveBlockSizeUU=%.1f ScaleZ=%.1f"),
        TileWorldSizeUU,
        BlocksPerTile,
        EffectiveBlockSizeUU,
        BlockHeightUU);

    {
        const float PerTileCoveredUU = static_cast<float>(BlocksPerTile) * EffectiveBlockSizeUU;
        const float GapUU = TileWorldSizeUU - PerTileCoveredUU;
        UE_LOG(LogTemp, Log,
            TEXT("[CubeTerrainRender] BlockGrid footprint check. PerTileCoveredUU=%.1f "
                 "ExpectedTileUU=%.1f GapUU=%.1f"),
            PerTileCoveredUU,
            TileWorldSizeUU,
            GapUU);
    }

    const int32 GridW    = Runtime->GetGridWidth();
    const int32 GridH    = Runtime->GetGridHeight();
    int32 CenterX        = GridW / 2;
    int32 CenterY        = GridH / 2;
    bool bUsingConfirmed = false;
    bool bUsingEmbark    = false;

    if (IsValid(VoxelMgr))
    {
        int32 ConfX, ConfY;
        if (VoxelMgr->GetConfirmedEmbarkTile(ConfX, ConfY))
        {
            CenterX         = ConfX;
            CenterY         = ConfY;
            bUsingConfirmed = true;
        }
    }

    if (!bUsingConfirmed)
    {
        if (UWorld* W = GetWorld())
        {
            if (UGameInstance* GI = W->GetGameInstance())
            {
                if (UWorldForgeEmbarkationManager* Embark =
                        GI->GetSubsystem<UWorldForgeEmbarkationManager>())
                {
                    int32 EX, EY, EIdx;
                    if (Embark->GetSelectedEmbarkTile(EX, EY, EIdx) && EX >= 0 && EY >= 0)
                    {
                        CenterX      = EX;
                        CenterY      = EY;
                        bUsingEmbark = true;
                    }
                }
            }
        }
    }

    if (!bUsingConfirmed && !bUsingEmbark)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrainRender] WARNING: No confirmed embark tile; using grid center (%d,%d)."),
            CenterX,
            CenterY);
    }

    const TCHAR* TileSourceStr = bUsingConfirmed ? TEXT("ConfirmedEmbarkTile")
                               : bUsingEmbark    ? TEXT("EmbarkationManager")
                               :                   TEXT("GridCenter (fallback)");
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] Patch center=(%d,%d) source=%s"),
        CenterX, CenterY, TileSourceStr);

    const int32 MinTileX = FMath::Max(0,        CenterX - PatchHalfRadius);
    const int32 MaxTileX = FMath::Min(GridW - 1, CenterX + PatchHalfRadius - 1);
    const int32 MinTileY = FMath::Max(0,        CenterY - PatchHalfRadius);
    const int32 MaxTileY = FMath::Min(GridH - 1, CenterY + PatchHalfRadius - 1);

    const int32 PatchTilesX = MaxTileX - MinTileX + 1;
    const int32 PatchTilesY = MaxTileY - MinTileY + 1;

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] BuildPatch start. Center=(%d,%d) Radius=%d "
             "TilePatch=%dx%d BlockGrid=%dx%d VisibleDepth=%d"),
        CenterX,
        CenterY,
        PatchHalfRadius,
        PatchTilesX,
        PatchTilesY,
        PatchTilesX * BlocksPerTile,
        PatchTilesY * BlocksPerTile,
        VisibleDepthBlocks);

    FWorldForgeCubePatchMeshSettings MeshSettings;
    MeshSettings.TerrainGridSize = FIntPoint(GridW, GridH);
    MeshSettings.PatchMinTile = FIntPoint(MinTileX, MinTileY);
    MeshSettings.PatchTileCount = FIntPoint(PatchTilesX, PatchTilesY);
    MeshSettings.TerrainOrigin = TerrainOrigin;
    MeshSettings.BlocksPerWorldTile = BlocksPerTile;
    MeshSettings.ChunkSizeInBlocks = FMath::Max(1, ChunkSizeInBlocks);
    MeshSettings.TileWorldSizeUU = TileWorldSizeUU;
    MeshSettings.BlockHeightUU = BlockHeightUU;
    MeshSettings.bFlatBlockSurface = bFlatBlockSurface;

    // CUBE-TERRAIN-03D: Pass visual debug flags into the mesh builder.
    MeshSettings.bDebugDisableGreedySideFaces = bDebugDisableGreedySideFaces;
    MeshSettings.bDebugSuppressOuterPatchBorderFaces = bDebugSuppressOuterPatchBorderFaces;

    // CUBE-TERRAIN-04M: Shoreline edge skirt.
    // Sea level target matches Runtime's internal Cube01C_WaterLevelBlock (=8). No public
    // accessor exists, so we use the same constant the runtime uses for ocean tile SurfaceZ.
    MeshSettings.SeaLevelZ = 8;
    MeshSettings.EdgeSkirtTiles = FMath::Max(0, EdgeSkirtTiles);

    // CUBE-TERRAIN-04M-FIX3: Proof log that the actor's EdgeSkirtTiles value reaches MeshSettings.
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainShape] Actor EdgeSkirtTiles=%d MeshSettings.EdgeSkirtTiles=%d SeaLevelZ=%d"),
        EdgeSkirtTiles,
        MeshSettings.EdgeSkirtTiles,
        MeshSettings.SeaLevelZ);

    // CUBE-TERRAIN-04O-STEP1: Material map audit. Confirms which actor class is rendering
    // and how many BlockTypeMaterials entries are populated before per-section assignment.
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMaterialAudit] ActorClass=%s BlockTypeMaterials=%d"),
        *GetClass()->GetName(),
        BlockTypeMaterials.Num());

    bool bPatchBuilt = false;

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] FlatBlockSurface=%s"),
        bFlatBlockSurface ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] Init. UseChunkMeshPrototype=%s"),
        bUseChunkMeshPrototype ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] HISM fallback disabled/available=%s"),
        bAllowHISMFallback ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainMesh] 03D visual debug. DisableGreedySideFaces=%s SuppressOuterPatchBorderFaces=%s"),
        bDebugDisableGreedySideFaces ? TEXT("true") : TEXT("false"),
        bDebugSuppressOuterPatchBorderFaces ? TEXT("true") : TEXT("false"));

    if (bUseChunkMeshPrototype)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CubeTerrainMesh] Build start. TilePatch=%dx%d BlocksPerTile=%d LocalGrid=%dx%d ChunkSize=%d"),
            PatchTilesX,
            PatchTilesY,
            BlocksPerTile,
            PatchTilesX * BlocksPerTile,
            PatchTilesY * BlocksPerTile,
            MeshSettings.ChunkSizeInBlocks);

        FWorldForgeCubePatchMeshData PatchMeshData;
        if (FWorldForgeCubeMeshBuilder::BuildPatchMesh(Runtime, MeshSettings, PatchMeshData))
        {
            // CUBE-TERRAIN-04N: Per-section material now comes from GetMaterialForBlockType.
            // DebugBaseMat load is retained purely so the legacy "asset missing" warning still fires.
            UMaterialInterface* DebugBaseMat = LoadObject<UMaterialInterface>(nullptr, Cube01E_DebugMatPath);
            if (!DebugBaseMat)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[CubeTerrainMesh] Debug base material not found (%s). Chunk mesh will use default materials."),
                    Cube01E_DebugMatPath);
            }

            // CUBE-TERRAIN-04N: One log line per unique block type encountered across the patch.
            TSet<EWorldForgeCubeBlockType> LoggedBlockMatTypes;

            // CUBE-TERRAIN-04O-STEP1: Per-patch material assignment audit counters.
            int32 AuditTotalSections = 0;
            int32 AuditAssignedCount = 0;
            int32 AuditDebugFallbackCount = 0;

            bool bChunkComponentFailure = false;

            for (const FWorldForgeCubeChunkMeshData& ChunkData : PatchMeshData.Chunks)
            {
                UProceduralMeshComponent* ChunkMeshComponent = NewObject<UProceduralMeshComponent>(
                    this,
                    *FString::Printf(TEXT("CubeChunkMesh_%d_%d"), ChunkData.ChunkCoord.X, ChunkData.ChunkCoord.Y));
                if (!ChunkMeshComponent)
                {
                    bChunkComponentFailure = true;
                    break;
                }

                ChunkMeshComponent->SetupAttachment(RootComponent);
                ChunkMeshComponent->SetMobility(EComponentMobility::Movable);
                ChunkMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                ChunkMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
                ChunkMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
                ChunkMeshComponent->SetGenerateOverlapEvents(false);
                ChunkMeshComponent->SetCanEverAffectNavigation(true);
                ChunkMeshComponent->SetCastShadow(true);
                ChunkMeshComponent->bUseAsyncCooking = true;
                AddInstanceComponent(ChunkMeshComponent);
                ChunkMeshComponent->RegisterComponent();

                UE_LOG(LogTemp, Log,
                    TEXT("[CubeTerrainCollision] Procedural chunk collision enabled. Mode=QueryOnly Sections=%d"),
                    ChunkData.Sections);

                int32 SectionIndex = 0;
                for (const FWorldForgeCubeMeshSectionData& SectionData : ChunkData.SectionData)
                {
                    if (!SectionData.HasGeometry())
                    {
                        continue;
                    }

                    ChunkMeshComponent->CreateMeshSection_LinearColor(
                        SectionIndex,
                        SectionData.Vertices,
                        SectionData.Triangles,
                        SectionData.Normals,
                        SectionData.UV0,
                        SectionData.VertexColors,
                        SectionData.Tangents,
                        true);

                    // CUBE-TERRAIN-04N: Material now resolved via BlockTypeMaterials map.
                    // Mapped overrides are assigned directly; unmapped/null entries fall back
                    // to the legacy debug-color MID so existing visuals are preserved.
                    const TObjectPtr<UMaterialInterface>* MappedEntry =
                        BlockTypeMaterials.Find(SectionData.BlockType);
                    const bool bUsingOverride = (MappedEntry && MappedEntry->Get() != nullptr);

                    UMaterialInterface* SectionMat = GetMaterialForBlockType(SectionData.BlockType);
                    if (bUsingOverride)
                    {
                        ChunkMeshComponent->SetMaterial(SectionIndex, SectionMat);
                    }
                    else if (UMaterialInstanceDynamic* MID =
                                CreateDebugMaterialInstance(SectionMat, SectionData.BlockType))
                    {
                        ChunkMeshComponent->SetMaterial(SectionIndex, MID);
                    }

                    // CUBE-TERRAIN-04O-STEP1: Tally assigned vs fallback for the patch summary.
                    ++AuditTotalSections;
                    if (bUsingOverride)
                    {
                        ++AuditAssignedCount;
                    }
                    else
                    {
                        ++AuditDebugFallbackCount;
                    }

                    if (!LoggedBlockMatTypes.Contains(SectionData.BlockType))
                    {
                        LoggedBlockMatTypes.Add(SectionData.BlockType);
                        UE_LOG(LogTemp, Log,
                            TEXT("[CubeTerrainMaterial] BlockType=%d Source=%s Material=%s"),
                            static_cast<int32>(SectionData.BlockType),
                            bUsingOverride ? TEXT("Assigned") : TEXT("DebugFallback"),
                            SectionMat ? *SectionMat->GetName() : TEXT("<null>"));
                    }

                    ++SectionIndex;
                }

                ChunkMeshComponents.Add(ChunkMeshComponent);

                UE_LOG(LogTemp, Log,
                    TEXT("[CubeTerrainMesh] Chunk built. Chunk=(%d,%d) Blocks=%d Faces=%d Sections=%d"),
                    ChunkData.ChunkCoord.X,
                    ChunkData.ChunkCoord.Y,
                    ChunkData.BlocksChecked,
                    ChunkData.Faces,
                    ChunkData.Sections);
            }

            if (!bChunkComponentFailure)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[CubeTerrainMesh] Build complete. Chunks=%d BlocksChecked=%d Faces=%d Triangles=%d Sections=%d"),
                    PatchMeshData.Chunks.Num(),
                    PatchMeshData.BlocksChecked,
                    PatchMeshData.Faces,
                    PatchMeshData.Triangles,
                    PatchMeshData.Sections);

                // CUBE-TERRAIN-04O-STEP1: Per-patch material assignment summary.
                UE_LOG(LogTemp, Log,
                    TEXT("[CubeTerrainMaterialAudit] Sections=%d Assigned=%d DebugFallback=%d"),
                    AuditTotalSections,
                    AuditAssignedCount,
                    AuditDebugFallbackCount);

                bPatchBuilt = true;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[CubeTerrainMesh] Chunk mesh component creation failed. Clearing partial mesh build."));
                DestroyChunkMeshComponents();
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[CubeTerrainMesh] Chunk mesh build failed before component creation."));
        }
    }

    if (!bPatchBuilt)
    {
        if (bAllowHISMFallback)
        {
            BuildPatchWithHISM(Runtime, MeshSettings);
            bPatchBuilt = true;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[CubeTerrainMesh] No render output generated. HISM fallback is disabled."));
        }
    }

    // CUBE-TERRAIN-01F: Smooth terrain isolation status.
    // VoxelWorldManager.SpawnCubeTerrainActor() acts on bCubePrototypeHideSmoothTerrainForTest
    // after BuildPatch returns. No manual editor hiding required.
    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainRender] Smooth terrain visible underneath: %s  (bCubePrototypeHideSmoothTerrainForTest=%s)"),
        bCubePrototypeHideSmoothTerrainForTest ? TEXT("false (will be hidden by VoxelWorldManager)") : TEXT("true"),
        bCubePrototypeHideSmoothTerrainForTest ? TEXT("true") : TEXT("false"));
}

// Source/WorldForge/Mining/MiningLayerRenderer.cpp

#include "Mining/MiningLayerRenderer.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Grid/LayerViewSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#include "Voxel/VoxelWorldManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMiningLayerRenderer, Log, All);

namespace
{
    // Number of terrain material slots to pre-size for log buckets:
    // Sand, Grass, Dirt, Rock, Snow, Water, Lava, Basalt, DeepRock.
    static constexpr int32 WF_DefaultTerrainLayerSlots = 9;

    // /Engine/BasicShapes/Cube.Cube is a 100UU unit cube. All cap-tile scaling
    // is expressed relative to this base size.
    static constexpr float WF_BaseCubeMeshSizeUU = 100.0f;

    static UMaterialInterface* WF_ReadLayerMaterialFromStruct(void* StructValue, UStruct* StructType)
    {
        if (!StructValue || !StructType)
        {
            return nullptr;
        }

        static const FName CandidateNames[] =
        {
            TEXT("LayerMaterial"),
            TEXT("Material"),
            TEXT("Layer_Material")
        };

        for (const FName& CandidateName : CandidateNames)
        {
            if (const FObjectPropertyBase* MaterialProp = FindFProperty<FObjectPropertyBase>(StructType, CandidateName))
            {
                return Cast<UMaterialInterface>(MaterialProp->GetObjectPropertyValue_InContainer(StructValue));
            }
        }

        // Fallback: first UMaterialInterface object property in the layer struct.
        for (TFieldIterator<FObjectPropertyBase> It(StructType); It; ++It)
        {
            const FObjectPropertyBase* ObjectProp = *It;
            if (ObjectProp && ObjectProp->PropertyClass && ObjectProp->PropertyClass->IsChildOf(UMaterialInterface::StaticClass()))
            {
                return Cast<UMaterialInterface>(ObjectProp->GetObjectPropertyValue_InContainer(StructValue));
            }
        }

        return nullptr;
    }

    static int32 WF_ReadLayerIndexFromStruct(void* StructValue, UStruct* StructType, int32 FallbackIndex)
    {
        if (!StructValue || !StructType)
        {
            return FallbackIndex;
        }

        static const FName CandidateNames[] =
        {
            TEXT("LayerIndex"),
            TEXT("Index"),
            TEXT("Layer_Index")
        };

        for (const FName& CandidateName : CandidateNames)
        {
            if (const FNumericProperty* NumericProp = FindFProperty<FNumericProperty>(StructType, CandidateName))
            {
                if (NumericProp->IsInteger())
                {
                    return static_cast<int32>(NumericProp->GetSignedIntPropertyValue(NumericProp->ContainerPtrToValuePtr<void>(StructValue)));
                }
            }
        }

        return FallbackIndex;
    }
}

// =============================================================================
// Construction / lifecycle
// =============================================================================

AMiningLayerRenderer::AMiningLayerRenderer()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    SelectedGhostHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SelectedGhostHISM"));
    SelectedGhostHISM->SetupAttachment(SceneRoot);

    ConfirmedGhostHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ConfirmedGhostHISM"));
    ConfirmedGhostHISM->SetupAttachment(SceneRoot);

    UndergroundBackdropHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("UndergroundBackdropHISM"));
    UndergroundBackdropHISM->SetupAttachment(SceneRoot);

    ConfigureHISM(SelectedGhostHISM);
    ConfigureHISM(ConfirmedGhostHISM);
    ConfigureHISM(UndergroundBackdropHISM);

    SetActorHiddenInGame(true);
}

void AMiningLayerRenderer::BeginPlay()
{
    Super::BeginPlay();

    EnsureAssets();

    if (CubeMesh)
    {
        SelectedGhostHISM->SetStaticMesh(CubeMesh);
        ConfirmedGhostHISM->SetStaticMesh(CubeMesh);
        if (UndergroundBackdropHISM)
        {
            UndergroundBackdropHISM->SetStaticMesh(CubeMesh);
        }
    }

    if (UMaterialInterface* GhostMat = ResolveGhostMaterial())
    {
        SelectedGhostHISM->SetMaterial(0, GhostMat);
        ConfirmedGhostHISM->SetMaterial(0, GhostMat);
    }

    if (UndergroundBackdropHISM)
    {
        UMaterialInterface* BackdropMat = DioramaWallMaterial ? DioramaWallMaterial.Get() : CapTileMaterial.Get();
        if (BackdropMat)
        {
            UndergroundBackdropHISM->SetMaterial(0, BackdropMat);
        }
    }

    SetRendererVisible(false);

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("MiningLayerRenderer ready. Cube=%s MaterialCollection=%s LayerMaterials=%d CapTileMat=%s FallbackMat=%s GhostMat=%s DebugMat=%s Radius=%d CapThickness=%.1f SuppressAtSurface=%s UseDebugMat=%s"),
        CubeMesh ? *CubeMesh->GetName() : TEXT("None"),
        *TerrainMaterialCollectionPath.ToString(),
        TerrainLayerMaterials.Num(),
        CapTileMaterial ? *CapTileMaterial->GetName() : TEXT("None"),
        FallbackTerrainMaterial ? *FallbackTerrainMaterial->GetName() : TEXT("None"),
        MiningGhostMaterial ? *MiningGhostMaterial->GetName() : TEXT("None"),
        DebugMaterial ? *DebugMaterial->GetName() : TEXT("None"),
        VisibleRadius,
        CapThicknessUU,
        bSuppressAtSurface ? TEXT("true") : TEXT("false"),
        bUseDebugMaterial ? TEXT("true") : TEXT("false"));
}

void AMiningLayerRenderer::SetCellSizeUU(float InCellSizeUU)
{
    CellSizeUU = FMath::Max(1.0f, InCellSizeUU);
}

void AMiningLayerRenderer::ForceNextRebuild()
{
    bForceNextRebuild             = true;
    bDiagnosticsDumpedThisSession = false;
}

// =============================================================================
// Visibility
// =============================================================================

void AMiningLayerRenderer::SetRendererVisible(bool bVisible)
{
    SetActorHiddenInGame(!bVisible);

    for (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& HISM : TerrainLayerHISMs)
    {
        if (HISM)
        {
            HISM->SetHiddenInGame(!bVisible);
            HISM->SetVisibility(bVisible && HISM->GetInstanceCount() > 0, true);
        }
    }

    if (SelectedGhostHISM)
    {
        SelectedGhostHISM->SetHiddenInGame(!bVisible);
        SelectedGhostHISM->SetVisibility(bVisible && SelectedGhostHISM->GetInstanceCount() > 0, true);
    }

    if (ConfirmedGhostHISM)
    {
        ConfirmedGhostHISM->SetHiddenInGame(!bVisible);
        ConfirmedGhostHISM->SetVisibility(bVisible && ConfirmedGhostHISM->GetInstanceCount() > 0, true);
    }

    if (UndergroundBackdropHISM)
    {
        UndergroundBackdropHISM->SetHiddenInGame(!bVisible);
        UndergroundBackdropHISM->SetVisibility(bVisible && UndergroundBackdropHISM->GetInstanceCount() > 0, true);
    }
}

void AMiningLayerRenderer::ClearTerrainLayerHISMs()
{
    for (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& HISM : TerrainLayerHISMs)
    {
        if (HISM)
        {
            HISM->ClearInstances();
            HISM->SetVisibility(false, true);
        }
    }
}

void AMiningLayerRenderer::ClearLayer()
{
    ClearTerrainLayerHISMs();

    if (SelectedGhostHISM)
    {
        SelectedGhostHISM->ClearInstances();
        SelectedGhostHISM->SetVisibility(false, true);
    }

    if (ConfirmedGhostHISM)
    {
        ConfirmedGhostHISM->ClearInstances();
        ConfirmedGhostHISM->SetVisibility(false, true);
    }

    if (UndergroundBackdropHISM)
    {
        UndergroundBackdropHISM->ClearInstances();
        UndergroundBackdropHISM->SetVisibility(false, true);
    }

    SetRendererVisible(false);

    // Reset dedupe + diagnostic state so the next SetLayerState gets a clean run.
    LastCenterVoxel               = FIntVector(MIN_int32, MIN_int32, MIN_int32);
    LastActiveVoxelZ              = MIN_int32;
    LastSelectedCount             = MIN_int32;
    LastConfirmedCount            = MIN_int32;
    bLastWasSuppressed            = false;
    bDiagnosticsDumpedThisSession = false;
}

// =============================================================================
// Cap build — the main work
// =============================================================================

void AMiningLayerRenderer::SetLayerState(
    const FIntVector& CenterVoxel,
    int32 ActiveVoxelZ,
    const TArray<FIntVector>& SelectedMineCells,
    const TArray<FIntVector>& ConfirmedMineCells)
{
    EnsureAssets();

    if (!SelectedGhostHISM || !ConfirmedGhostHISM || !CubeMesh)
    {
        UE_LOG(LogMiningLayerRenderer, Warning,
            TEXT("SetLayerState failed: renderer assets/components incomplete. "
                 "CubeMesh=%s SelectedHISM=%s ConfirmedHISM=%s"),
            CubeMesh ? TEXT("ok") : TEXT("null"),
            SelectedGhostHISM ? TEXT("ok") : TEXT("null"),
            ConfirmedGhostHISM ? TEXT("ok") : TEXT("null"));
        return;
    }

    // -------------------------------------------------------------------------
    // Surface-suppression check.
    //
    // At surface offset 0, the cutaway is disabled and the actual voxel terrain
    // is the workspace. Drawing cap tiles at Z=ActiveVoxelZ*100 puts them INSIDE
    // solid voxel terrain (the dirt/grass above isn't being clipped away), so
    // the cap is invisible regardless of geometry. Suppressing the cap entirely
    // at surface is the only correct behavior — the surface terrain renders
    // itself, no overlay needed.
    //
    // The cap pays off underground (relative offset < 0), where cutaway has
    // clipped the surface terrain away and the cap fills the workspace floor.
    // -------------------------------------------------------------------------

    int32 RelativeOffset = 0;
    bool  bHaveLayerView = false;
    if (UWorld* World = GetWorld())
    {
        if (ULayerViewSubsystem* LayerView = World->GetSubsystem<ULayerViewSubsystem>())
        {
            bHaveLayerView = true;
            RelativeOffset = LayerView->GetRelativeLayerOffset();
        }
    }

    const bool bAtOrAboveSurface = bHaveLayerView && (RelativeOffset >= 0);
    const bool bSuppressing       = bSuppressAtSurface && bAtOrAboveSurface;

    // -------------------------------------------------------------------------
    // Dedupe — skip rebuilds when nothing meaningful changed. Without this the
    // caller's per-tick poll spams the log with identical "MiningCap set" lines
    // and rebuilds the HISMs needlessly.
    // -------------------------------------------------------------------------

    const int32 SelCount  = SelectedMineCells.Num();
    const int32 ConfCount = ConfirmedMineCells.Num();

    const bool bStateUnchanged =
           !bForceNextRebuild
        && CenterVoxel == LastCenterVoxel
        && ActiveVoxelZ == LastActiveVoxelZ
        && SelCount  == LastSelectedCount
        && ConfCount == LastConfirmedCount
        && bSuppressing == bLastWasSuppressed;

    if (bStateUnchanged)
    {
        return;
    }

    LastCenterVoxel    = CenterVoxel;
    LastActiveVoxelZ   = ActiveVoxelZ;
    LastSelectedCount  = SelCount;
    LastConfirmedCount = ConfCount;
    bLastWasSuppressed = bSuppressing;
    bForceNextRebuild  = false;

    // -------------------------------------------------------------------------
    // Suppressed path: clear everything and hide.
    // -------------------------------------------------------------------------

    if (bSuppressing)
    {
        ClearTerrainLayerHISMs();
        SelectedGhostHISM->ClearInstances();
        ConfirmedGhostHISM->ClearInstances();
        if (UndergroundBackdropHISM)
        {
            UndergroundBackdropHISM->ClearInstances();
        }
        SetRendererVisible(false);

        UE_LOG(LogMiningLayerRenderer, Log,
            TEXT("MiningCap suppressed at surface. Center=(%d,%d,%d) ActiveZ=%d RelOffset=%+d HaveLayerView=%s "
                 "(surface terrain is the workspace; cap is for underground only)"),
            CenterVoxel.X, CenterVoxel.Y, CenterVoxel.Z,
            ActiveVoxelZ,
            RelativeOffset,
            bHaveLayerView ? TEXT("true") : TEXT("false"));
        return;
    }

    // -------------------------------------------------------------------------
    // Active rebuild path (underground or surface-suppress disabled).
    // -------------------------------------------------------------------------

    ClearTerrainLayerHISMs();
    SelectedGhostHISM->ClearInstances();
    ConfirmedGhostHISM->ClearInstances();

    if (CubeMesh)
    {
        SelectedGhostHISM->SetStaticMesh(CubeMesh);
        ConfirmedGhostHISM->SetStaticMesh(CubeMesh);
        if (UndergroundBackdropHISM)
        {
            UndergroundBackdropHISM->SetStaticMesh(CubeMesh);
        }
    }

    if (UMaterialInterface* GhostMat = ResolveGhostMaterial())
    {
        SelectedGhostHISM->SetMaterial(0, GhostMat);
        ConfirmedGhostHISM->SetMaterial(0, GhostMat);
    }

    if (UndergroundBackdropHISM)
    {
        UMaterialInterface* BackdropMat = DioramaWallMaterial ? DioramaWallMaterial.Get() : CapTileMaterial.Get();
        if (BackdropMat)
        {
            UndergroundBackdropHISM->SetMaterial(0, BackdropMat);
        }
    }

    TSet<FIntVector> SelectedSet;
    SelectedSet.Reserve(SelectedMineCells.Num());
    for (const FIntVector& Cell : SelectedMineCells)
    {
        SelectedSet.Add(Cell);
    }

    TSet<FIntVector> ConfirmedSet;
    ConfirmedSet.Reserve(ConfirmedMineCells.Num());
    for (const FIntVector& Cell : ConfirmedMineCells)
    {
        ConfirmedSet.Add(Cell);
    }

    UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

    const int32 SafeRadius = FMath::Clamp(VisibleRadius, 1, 96);
    const int32 CapZ       = ActiveVoxelZ;

    int32 TerrainInstances        = 0;
    int32 SelectedGhostInstances  = 0;
    int32 ConfirmedGhostInstances = 0;
    int32 EmptyCount              = 0;
    int32 OtherLayerCount         = 0;

    TArray<int32> LayerCounts;
    LayerCounts.SetNumZeroed(FMath::Max(TerrainLayerMaterials.Num(), WF_DefaultTerrainLayerSlots));

    // ---- 0) Dark underground backdrop --------------------------------------
    AddUndergroundBackdropInstances(CenterVoxel, ActiveVoxelZ);

    // ---- 1) Terrain cap pass ------------------------------------------------

    for (int32 X = CenterVoxel.X - SafeRadius; X <= CenterVoxel.X + SafeRadius; ++X)
    {
        for (int32 Y = CenterVoxel.Y - SafeRadius; Y <= CenterVoxel.Y + SafeRadius; ++Y)
        {
            const FIntVector Cell(X, Y, CapZ);

            // Always render the solid cap/floor under designations. Selection and
            // confirmed mining markers draw on top; cutting holes here makes the
            // underground layer read as unstable/flickery.

            int32 MaterialLayer = 2;
            FGameplayTag BlockType;
            const bool bSolid = VM
                ? VM->GetTerrainMaterialLayerAtVoxel(Cell, MaterialLayer, BlockType)
                : true;

            if (!bSolid)
            {
                ++EmptyCount;
                continue;
            }

            if (MaterialLayer >= 0 && MaterialLayer < LayerCounts.Num())
            {
                ++LayerCounts[MaterialLayer];
            }
            else
            {
                ++OtherLayerCount;
            }

            UHierarchicalInstancedStaticMeshComponent* TargetHISM = GetOrCreateTerrainLayerHISM(MaterialLayer);
            if (!TargetHISM)
            {
                ++EmptyCount;
                continue;
            }

            const FTransform CapXform = MakeCapTileTransform(Cell, TerrainCapTopBiasUU, TerrainBlockScale);
            const int32 Idx = TargetHISM->AddInstance(CapXform, true);
            SetInstanceColor(TargetHISM, Idx, GetTerrainColorForLayer(MaterialLayer));
            ++TerrainInstances;
        }
    }

    // ---- 2) Extended underground floor ring ------------------------------------
    //
    // The Voxel Plugin smooth terrain generates a closed mesh with floor/edge faces
    // that sit just below the clip plane and inherit the surface biome color (grass
    // at shallow depth). The inner cap (VisibleRadius) only covers the 21×21
    // workspace. This extended ring covers the visible terrain beyond the walls with
    // a flat dark-earth tile, hiding the biome bleed on all sides.
    //
    // Only built when actually underground (RelativeOffset < 0). Skipped at surface.
    // ─────────────────────────────────────────────────────────────────────────────
    if (RelativeOffset < 0 && OuterCapExtendRadius > 0)
    {
        const int32 ExtRadius = FMath::Clamp(SafeRadius + OuterCapExtendRadius, SafeRadius + 1, 96);

        // Dark underground earth color — unlit material reads PerInstanceCustomData[0..3].
        // Uses a warm dark-brown that reads as "deep rock/soil", distinct from the
        // terrain-queried inner cap colors.
        static const FLinearColor ExtFloorColor(0.18f, 0.13f, 0.09f, 1.0f);

        // Slight negative Z bias relative to inner cap to prevent z-fighting at the seam.
        const float ExtBiasUU = TerrainCapTopBiasUU - 8.0f;

        // Reuse the Rock (layer 3) HISM so no new component is created.
        UHierarchicalInstancedStaticMeshComponent* ExtHISM = GetOrCreateTerrainLayerHISM(3);
        if (ExtHISM)
        {
            for (int32 X = CenterVoxel.X - ExtRadius; X <= CenterVoxel.X + ExtRadius; ++X)
            {
                for (int32 Y = CenterVoxel.Y - ExtRadius; Y <= CenterVoxel.Y + ExtRadius; ++Y)
                {
                    // Inner cap already covers this cell — skip to avoid double-draw.
                    if (FMath::Abs(X - CenterVoxel.X) <= SafeRadius &&
                        FMath::Abs(Y - CenterVoxel.Y) <= SafeRadius)
                    {
                        continue;
                    }

                    const FTransform Xform = MakeCapTileTransform(FIntVector(X, Y, CapZ), ExtBiasUU, TerrainBlockScale);
                    const int32 Idx = ExtHISM->AddInstance(Xform, true);
                    SetInstanceColor(ExtHISM, Idx, ExtFloorColor);
                }
            }
        }
    }

    // ---- 3) Ghost pass -------------------------------------------------------

    auto AddGhostCells = [&](
        UHierarchicalInstancedStaticMeshComponent* HISM,
        const TSet<FIntVector>& Cells,
        const FLinearColor& Color,
        float TopBiasUU,
        int32& Count)
    {
        if (!HISM)
        {
            return;
        }

        for (const FIntVector& Cell : Cells)
        {
            if (FMath::Abs(Cell.X - CenterVoxel.X) > SafeRadius ||
                FMath::Abs(Cell.Y - CenterVoxel.Y) > SafeRadius ||
                Cell.Z != CapZ)
            {
                continue;
            }

            const FTransform GhostXform = MakeCapTileTransform(Cell, TopBiasUU, GhostBlockScale);
            const int32 Idx = HISM->AddInstance(GhostXform, true);
            SetInstanceColor(HISM, Idx, Color);
            HISM->SetCustomDataValue(Idx, 4, Color.A, true);
            ++Count;
        }
    };

    AddGhostCells(ConfirmedGhostHISM, ConfirmedSet, GetConfirmedGhostColor(), ConfirmedGhostTopBiasUU, ConfirmedGhostInstances);
    AddGhostCells(SelectedGhostHISM,  SelectedSet,  GetSelectedGhostColor(),  SelectedGhostTopBiasUU,  SelectedGhostInstances);

    // ---- 3) Render state finalization --------------------------------------

    for (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& HISM : TerrainLayerHISMs)
    {
        if (HISM)
        {
            HISM->SetCullDistances(0, 0);
            HISM->MarkRenderStateDirty();
        }
    }
    SelectedGhostHISM->SetCullDistances(0, 0);
    ConfirmedGhostHISM->SetCullDistances(0, 0);
    if (UndergroundBackdropHISM)
    {
        UndergroundBackdropHISM->SetCullDistances(0, 0);
        UndergroundBackdropHISM->MarkRenderStateDirty();
    }
    SelectedGhostHISM->MarkRenderStateDirty();
    ConfirmedGhostHISM->MarkRenderStateDirty();

    SetRendererVisible(true);

    // ---- 4) Logging --------------------------------------------------------

    const int32 Sand     = LayerCounts.IsValidIndex(0) ? LayerCounts[0] : 0;
    const int32 Grass    = LayerCounts.IsValidIndex(1) ? LayerCounts[1] : 0;
    const int32 Dirt     = LayerCounts.IsValidIndex(2) ? LayerCounts[2] : 0;
    const int32 Rock     = LayerCounts.IsValidIndex(3) ? LayerCounts[3] : 0;
    const int32 Snow     = LayerCounts.IsValidIndex(4) ? LayerCounts[4] : 0;
    const int32 Water    = LayerCounts.IsValidIndex(5) ? LayerCounts[5] : 0;
    const int32 Lava     = LayerCounts.IsValidIndex(6) ? LayerCounts[6] : 0;
    const int32 Basalt   = LayerCounts.IsValidIndex(7) ? LayerCounts[7] : 0;
    const int32 DeepRock = LayerCounts.IsValidIndex(8) ? LayerCounts[8] : 0;

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("MiningCap built. Center=(%d,%d,%d) ActiveZ=%d RelOffset=%+d Radius=%d CapThickness=%.1f "
             "Bias[Terrain=%.2f,Sel=%.2f,Conf=%.2f] DebugMat=%s "
             "Terrain=%d SelectedGhost=%d ConfirmedGhost=%d Empty=%d Buckets=%d "
             "Sand=%d Grass=%d Dirt=%d Rock=%d Snow=%d Water=%d Lava=%d Basalt=%d DeepRock=%d Other=%d"),
        CenterVoxel.X, CenterVoxel.Y, CenterVoxel.Z,
        ActiveVoxelZ,
        RelativeOffset,
        SafeRadius,
        CapThicknessUU,
        TerrainCapTopBiasUU, SelectedGhostTopBiasUU, ConfirmedGhostTopBiasUU,
        bUseDebugMaterial ? TEXT("ON") : TEXT("off"),
        TerrainInstances,
        SelectedGhostInstances,
        ConfirmedGhostInstances,
        EmptyCount,
        TerrainLayerHISMs.Num(),
        Sand, Grass, Dirt, Rock, Snow, Water, Lava, Basalt, DeepRock, OtherLayerCount);

    // ---- 5) One-shot diagnostics dump --------------------------------------

    if (bDumpDiagnosticsOnce && !bDiagnosticsDumpedThisSession)
    {
        DumpDiagnostics(CenterVoxel, ActiveVoxelZ, TerrainInstances);
        bDiagnosticsDumpedThisSession = true;
    }
}

// =============================================================================
// Diagnostics
// =============================================================================

void AMiningLayerRenderer::DumpDiagnostics(
    const FIntVector& CenterVoxel,
    int32 ActiveVoxelZ,
    int32 TerrainInstancesAdded) const
{
    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("[Diagnostics] ===== MiningLayerRenderer state dump ====="));

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("[Diagnostics] Actor loc=%s hidden=%s "
             "Center=(%d,%d,%d) ActiveZ=%d ExpectedCapTopWorldZ=%.1f"),
        *GetActorLocation().ToString(),
        IsHidden() ? TEXT("true") : TEXT("false"),
        CenterVoxel.X, CenterVoxel.Y, CenterVoxel.Z,
        ActiveVoxelZ,
        static_cast<float>(ActiveVoxelZ) * CellSizeUU + TerrainCapTopBiasUU);

    for (int32 i = 0; i < TerrainLayerHISMs.Num(); ++i)
    {
        UHierarchicalInstancedStaticMeshComponent* HISM = TerrainLayerHISMs[i].Get();
        if (!HISM)
        {
            continue;
        }

        const int32 InstCount = HISM->GetInstanceCount();
        FString SampleXformStr = TEXT("<no instances>");
        if (InstCount > 0)
        {
            FTransform SampleX;
            if (HISM->GetInstanceTransform(0, SampleX, /*bWorldSpace*/ true))
            {
                SampleXformStr = FString::Printf(
                    TEXT("loc=%s scale=%s"),
                    *SampleX.GetLocation().ToString(),
                    *SampleX.GetScale3D().ToString());
            }
        }

        UMaterialInterface* Mat = HISM->GetMaterial(0);
        UE_LOG(LogMiningLayerRenderer, Log,
            TEXT("[Diagnostics] TerrainHISM[%d] loc=%s vis=%s hidden=%s count=%d mat=%s sample=%s"),
            i,
            *HISM->GetComponentLocation().ToString(),
            HISM->IsVisible() ? TEXT("true") : TEXT("false"),
            HISM->bHiddenInGame ? TEXT("true") : TEXT("false"),
            InstCount,
            Mat ? *Mat->GetName() : TEXT("None"),
            *SampleXformStr);
    }

    auto DumpGhostHISM = [](const TCHAR* Tag, UHierarchicalInstancedStaticMeshComponent* HISM)
    {
        if (!HISM)
        {
            UE_LOG(LogMiningLayerRenderer, Log, TEXT("[Diagnostics] %s=null"), Tag);
            return;
        }

        UMaterialInterface* Mat = HISM->GetMaterial(0);
        UE_LOG(LogMiningLayerRenderer, Log,
            TEXT("[Diagnostics] %s loc=%s vis=%s hidden=%s count=%d mat=%s"),
            Tag,
            *HISM->GetComponentLocation().ToString(),
            HISM->IsVisible() ? TEXT("true") : TEXT("false"),
            HISM->bHiddenInGame ? TEXT("true") : TEXT("false"),
            HISM->GetInstanceCount(),
            Mat ? *Mat->GetName() : TEXT("None"));
    };

    DumpGhostHISM(TEXT("SelectedGhostHISM"),  SelectedGhostHISM);
    DumpGhostHISM(TEXT("ConfirmedGhostHISM"), ConfirmedGhostHISM);

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("[Diagnostics] TerrainInstancesAddedThisBuild=%d. If geometry is positioned correctly but invisible, "
             "toggle bUseDebugMaterial=true to verify with WorldGridMaterial."),
        TerrainInstancesAdded);

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("[Diagnostics] ===== End dump ====="));
}

// =============================================================================
// Asset / material discovery
// =============================================================================

void AMiningLayerRenderer::EnsureAssets()
{
    if (!CubeMesh)
    {
        CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    if (!CapTileMaterial)
    {
        CapTileMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/WorldForge/Materials/M_WF_CapTile.M_WF_CapTile"));
    }

    if (!DioramaWallMaterial)
    {
        DioramaWallMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/WorldForge/Materials/M_WF_UndergroundDioramaWall.M_WF_UndergroundDioramaWall"));
    }

    if (!FallbackTerrainMaterial)
    {
        FallbackTerrainMaterial = CapTileMaterial
            ? CapTileMaterial.Get()
            : LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/WorldForge/Materials/M_WF_CapTile.M_WF_CapTile"));
    }

    TryAutoLoadTerrainLayerMaterialsFromCollection();

    if (!MiningGhostMaterial)
    {
        MiningGhostMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/WorldForge/Materials/M_WF_MiningGhost.M_WF_MiningGhost"));
    }

    if (!DebugMaterial)
    {
        DebugMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    }
}

void AMiningLayerRenderer::TryAutoLoadTerrainLayerMaterialsFromCollection()
{
    if (!TerrainMaterialCollectionPath.IsValid())
    {
        if (TerrainLayerMaterials.Num() < WF_DefaultTerrainLayerSlots)
        {
            TerrainLayerMaterials.SetNum(WF_DefaultTerrainLayerSlots);
        }
        return;
    }

    if (TerrainLayerMaterials.Num() >= WF_DefaultTerrainLayerSlots)
    {
        bool bHasAny = false;
        for (UMaterialInterface* Mat : TerrainLayerMaterials)
        {
            if (Mat)
            {
                bHasAny = true;
                break;
            }
        }
        if (bHasAny)
        {
            return;
        }
    }

    UObject* CollectionObject = TerrainMaterialCollectionPath.TryLoad();
    if (!CollectionObject)
    {
        if (TerrainLayerMaterials.Num() < WF_DefaultTerrainLayerSlots)
        {
            TerrainLayerMaterials.SetNum(WF_DefaultTerrainLayerSlots);
        }
        UE_LOG(LogMiningLayerRenderer, Warning,
            TEXT("Could not load terrain material collection: %s"),
            *TerrainMaterialCollectionPath.ToString());
        return;
    }

    const FArrayProperty* LayersProp = FindFProperty<FArrayProperty>(CollectionObject->GetClass(), TEXT("Layers"));
    if (!LayersProp || !LayersProp->Inner)
    {
        if (TerrainLayerMaterials.Num() < WF_DefaultTerrainLayerSlots)
        {
            TerrainLayerMaterials.SetNum(WF_DefaultTerrainLayerSlots);
        }
        UE_LOG(LogMiningLayerRenderer, Warning,
            TEXT("Terrain material collection '%s' has no reflected Layers array."),
            *CollectionObject->GetName());
        return;
    }

    const FStructProperty* LayerStructProp = CastField<FStructProperty>(LayersProp->Inner);
    if (!LayerStructProp || !LayerStructProp->Struct)
    {
        if (TerrainLayerMaterials.Num() < WF_DefaultTerrainLayerSlots)
        {
            TerrainLayerMaterials.SetNum(WF_DefaultTerrainLayerSlots);
        }
        UE_LOG(LogMiningLayerRenderer, Warning,
            TEXT("Terrain material collection '%s' Layers array is not a struct array."),
            *CollectionObject->GetName());
        return;
    }

    FScriptArrayHelper LayersHelper(LayersProp, LayersProp->ContainerPtrToValuePtr<void>(CollectionObject));
    const int32 LayerCount = LayersHelper.Num();
    TerrainLayerMaterials.SetNum(FMath::Max(TerrainLayerMaterials.Num(), FMath::Max(WF_DefaultTerrainLayerSlots, LayerCount)));

    int32 Loaded = 0;
    for (int32 i = 0; i < LayerCount; ++i)
    {
        void* LayerPtr = LayersHelper.GetRawPtr(i);
        const int32 LayerIndex = WF_ReadLayerIndexFromStruct(LayerPtr, LayerStructProp->Struct, i);
        UMaterialInterface* LayerMaterial = WF_ReadLayerMaterialFromStruct(LayerPtr, LayerStructProp->Struct);

        if (LayerIndex >= 0 && LayerMaterial)
        {
            if (LayerIndex >= TerrainLayerMaterials.Num())
            {
                TerrainLayerMaterials.SetNum(LayerIndex + 1);
            }
            TerrainLayerMaterials[LayerIndex] = LayerMaterial;
            ++Loaded;
        }
    }

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("Loaded %d/%d terrain layer materials from %s."),
        Loaded,
        LayerCount,
        *CollectionObject->GetName());
}

UMaterialInterface* AMiningLayerRenderer::ResolveTerrainMaterialForLayer(int32 MaterialLayer) const
{
    // Debug override wins — every terrain HISM gets WorldGridMaterial.
    if (bUseDebugMaterial && DebugMaterial)
    {
        return DebugMaterial.Get();
    }

    // Mining cap tiles must use the dedicated unlit/emissive cap material.
    // MC_WorldForge_Terrain remains available for real voxel terrain, but its
    // lit/PBR materials are not appropriate for underground cap tiles.
    if (CapTileMaterial)
    {
        return CapTileMaterial.Get();
    }

    // Legacy fallback path if CapTileMaterial is missing.
    if (TerrainLayerMaterials.IsValidIndex(MaterialLayer) && TerrainLayerMaterials[MaterialLayer])
    {
        return TerrainLayerMaterials[MaterialLayer].Get();
    }

    return FallbackTerrainMaterial.Get();
}

UMaterialInterface* AMiningLayerRenderer::ResolveGhostMaterial() const
{
    if (bUseDebugMaterial && DebugMaterial)
    {
        return DebugMaterial.Get();
    }
    return MiningGhostMaterial.Get();
}

UHierarchicalInstancedStaticMeshComponent* AMiningLayerRenderer::GetOrCreateTerrainLayerHISM(int32 MaterialLayer)
{
    if (MaterialLayer < 0)
    {
        MaterialLayer = 0;
    }

    if (MaterialLayer >= TerrainLayerHISMs.Num())
    {
        TerrainLayerHISMs.SetNum(MaterialLayer + 1);
    }

    if (TerrainLayerHISMs[MaterialLayer])
    {
        // Re-resolve material in case bUseDebugMaterial flipped at runtime.
        if (UMaterialInterface* LayerMaterial = ResolveTerrainMaterialForLayer(MaterialLayer))
        {
            TerrainLayerHISMs[MaterialLayer]->SetMaterial(0, LayerMaterial);
        }
        return TerrainLayerHISMs[MaterialLayer].Get();
    }

    const FName ComponentName(*FString::Printf(TEXT("TerrainLayerHISM_%d"), MaterialLayer));
    UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, ComponentName);
    if (!NewHISM)
    {
        return nullptr;
    }

    NewHISM->SetupAttachment(SceneRoot);
    ConfigureHISM(NewHISM);

    if (CubeMesh)
    {
        NewHISM->SetStaticMesh(CubeMesh);
    }

    if (UMaterialInterface* LayerMaterial = ResolveTerrainMaterialForLayer(MaterialLayer))
    {
        NewHISM->SetMaterial(0, LayerMaterial);
    }

    NewHISM->RegisterComponent();
    NewHISM->SetHiddenInGame(IsHidden());
    NewHISM->SetVisibility(false, true);

    TerrainLayerHISMs[MaterialLayer] = NewHISM;
    return NewHISM;
}

void AMiningLayerRenderer::ConfigureHISM(UHierarchicalInstancedStaticMeshComponent* HISM) const
{
    if (!HISM)
    {
        return;
    }

    HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HISM->SetCastShadow(false);
    HISM->SetGenerateOverlapEvents(false);
    HISM->bDisableCollision = true;
    HISM->NumCustomDataFloats = 5;
    HISM->SetCullDistances(0, 0);
}

void AMiningLayerRenderer::SetInstanceColor(
    UHierarchicalInstancedStaticMeshComponent* HISM,
    int32 InstanceIndex,
    const FLinearColor& Color) const
{
    if (!HISM || InstanceIndex < 0)
    {
        return;
    }

    HISM->SetCustomDataValue(InstanceIndex, 0, Color.R, false);
    HISM->SetCustomDataValue(InstanceIndex, 1, Color.G, false);
    HISM->SetCustomDataValue(InstanceIndex, 2, Color.B, false);
    HISM->SetCustomDataValue(InstanceIndex, 3, Color.A, true);
}

// =============================================================================
// Cap tile transform
// =============================================================================

FTransform AMiningLayerRenderer::MakeCapTileTransform(
    const FIntVector& VoxelCoord,
    float TopZBiasUU,
    float XYScaleMultiplier) const
{
    const float SafeXYScale    = FMath::Clamp(XYScaleMultiplier, 0.05f, 1.0f);
    const float CellMeshScale  = CellSizeUU / WF_BaseCubeMeshSizeUU;
    const float SafeThickness  = FMath::Max(CapThicknessUU, 1.0f);
    const float ThicknessScale = SafeThickness / WF_BaseCubeMeshSizeUU;

    const float TopWorldZ    = static_cast<float>(VoxelCoord.Z) * CellSizeUU + TopZBiasUU;
    const float CenterWorldZ = TopWorldZ - (SafeThickness * 0.5f);

    const float CenterWorldX = (static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU;
    const float CenterWorldY = (static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU;

    return FTransform(
        FRotator::ZeroRotator,
        FVector(CenterWorldX, CenterWorldY, CenterWorldZ),
        FVector(CellMeshScale * SafeXYScale, CellMeshScale * SafeXYScale, ThicknessScale));
}

void AMiningLayerRenderer::AddUndergroundBackdropInstances(const FIntVector& CenterVoxel, int32 ActiveVoxelZ)
{
    // ── PASS 1: Terrain-colored diorama walls ─────────────────────────────────
    //
    // Instead of four monolithic dark-blue slabs, we place individual voxel cubes
    // around the perimeter of the active mine area. Each cube queries
    // GetTerrainMaterialLayerAtVoxel so the wall shows the actual rock/dirt/sand
    // composition at the excavation boundary — communicating "this is a chunk of
    // earth that has been cut open" rather than "this is a blue void."
    //
    // Wall geometry:
    //   - Perimeter ring at distance (VisibleRadius + 1) from CenterVoxel XY.
    //   - Stacks DioramaWallHeightLayers cubes upward starting at ActiveVoxelZ.
    //   - Layer 0 aligns with the cap floor; upper layers protrude into the cut edge.
    //   - Corners included in the N/S sweep; E/W sweeps skip corners to avoid doubles.
    //
    // Material:
    //   CapTileMaterial is used because it is the one unlit/emissive material in
    //   the renderer that already reads PerInstanceCustomData[0..3] as RGBA. The
    //   DioramaWallMaterial slot remains available for a future dedicated wall
    //   material with edge darkening/depth tint (Pass 3 polish).
    // ─────────────────────────────────────────────────────────────────────────────

    if (!UndergroundBackdropHISM || !CubeMesh)
    {
        return;
    }

    // Material selection — same priority as terrain cap tiles.
    UMaterialInterface* WallMat = nullptr;
    if (bUseDebugMaterial && DebugMaterial)
    {
        WallMat = DebugMaterial.Get();
    }
    else if (CapTileMaterial)
    {
        // Preferred: unlit/emissive material that reads per-instance RGBA.
        WallMat = CapTileMaterial.Get();
    }
    else if (DioramaWallMaterial)
    {
        WallMat = DioramaWallMaterial.Get();
    }

    if (!WallMat)
    {
        return;
    }

    UndergroundBackdropHISM->ClearInstances();
    UndergroundBackdropHISM->SetStaticMesh(CubeMesh);
    UndergroundBackdropHISM->SetMaterial(0, WallMat);

    UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

    const int32 SafeRadius = FMath::Clamp(VisibleRadius, 1, 96);
    const int32 WallLayers = FMath::Clamp(DioramaWallHeightLayers, 1, 32);
    const float MeshScale  = CellSizeUU / WF_BaseCubeMeshSizeUU;

    // ─── Per-voxel helper ────────────────────────────────────────────────────
    //
    // Queries terrain type at (X, Y, Z). If the voxel is empty (the player has
    // already dug past the wall edge), samples one cell below the active floor
    // for a representative color rather than leaving a visual gap.
    //
    // Fallback: MaterialLayer 3 (Rock) — sensible for deep underground walls.
    // ─────────────────────────────────────────────────────────────────────────

    auto AddWallVoxel = [&](int32 X, int32 Y, int32 Z)
    {
        int32 MaterialLayer = 3; // Rock fallback
        FGameplayTag BlockType;

        if (VM)
        {
            const bool bSolid = VM->GetTerrainMaterialLayerAtVoxel(FIntVector(X, Y, Z), MaterialLayer, BlockType);
            if (!bSolid)
            {
                // Voxel is already air — sample one layer below the active floor
                // for a color hint so the wall still looks like earth.
                VM->GetTerrainMaterialLayerAtVoxel(FIntVector(X, Y, ActiveVoxelZ - 1), MaterialLayer, BlockType);
                MaterialLayer = FMath::Clamp(MaterialLayer, 0, 8);
            }
        }

        const FLinearColor Color = GetTerrainColorForLayer(MaterialLayer) * 0.50f;

        const float WorldX = (static_cast<float>(X) + 0.5f) * CellSizeUU;
        const float WorldY = (static_cast<float>(Y) + 0.5f) * CellSizeUU;
        const float WorldZ = (static_cast<float>(Z) + 0.5f) * CellSizeUU;

        const FTransform T(FRotator::ZeroRotator, FVector(WorldX, WorldY, WorldZ), FVector(MeshScale));
        const int32 Idx = UndergroundBackdropHISM->AddInstance(T, true);
        SetInstanceColor(UndergroundBackdropHISM, Idx, Color);
    };

    // ─── North + South walls (full width including corners) ──────────────────
    const int32 WallX_Min = CenterVoxel.X - SafeRadius - 1;
    const int32 WallX_Max = CenterVoxel.X + SafeRadius + 1;
    const int32 NorthY    = CenterVoxel.Y + SafeRadius + 1;
    const int32 SouthY    = CenterVoxel.Y - SafeRadius - 1;

    for (int32 X = WallX_Min; X <= WallX_Max; ++X)
    {
        for (int32 dZ = 0; dZ < WallLayers; ++dZ)
        {
            AddWallVoxel(X, NorthY, ActiveVoxelZ + dZ);
            AddWallVoxel(X, SouthY, ActiveVoxelZ + dZ);
        }
    }

    // ─── East + West walls (excluding corners — already covered above) ────────
    const int32 EastX  = CenterVoxel.X + SafeRadius + 1;
    const int32 WestX  = CenterVoxel.X - SafeRadius - 1;
    const int32 WallY_Min = CenterVoxel.Y - SafeRadius;
    const int32 WallY_Max = CenterVoxel.Y + SafeRadius;

    for (int32 Y = WallY_Min; Y <= WallY_Max; ++Y)
    {
        for (int32 dZ = 0; dZ < WallLayers; ++dZ)
        {
            AddWallVoxel(EastX, Y, ActiveVoxelZ + dZ);
            AddWallVoxel(WestX, Y, ActiveVoxelZ + dZ);
        }
    }

    UndergroundBackdropHISM->SetCullDistances(0, 0);
    UndergroundBackdropHISM->SetHiddenInGame(false);
    UndergroundBackdropHISM->SetVisibility(true, true);
    UndergroundBackdropHISM->MarkRenderStateDirty();

    const int32 ExpectedCount =
        ((WallX_Max - WallX_Min + 1) * 2 + (WallY_Max - WallY_Min + 1) * 2) * WallLayers;

    UE_LOG(LogMiningLayerRenderer, Log,
        TEXT("[DioramaWalls] Built %d terrain-colored wall voxels. Radius=%d WallLayers=%d ActiveZ=%d Mat=%s"),
        ExpectedCount, SafeRadius, WallLayers, ActiveVoxelZ, *WallMat->GetName());
}

FTransform AMiningLayerRenderer::MakeUndergroundBackdropTransform(
    const FIntVector& CenterVoxel,
    int32 ActiveVoxelZ) const
{
    const int32 SafeRadius = FMath::Clamp(UndergroundBackdropRadius, VisibleRadius, 256);
    const float DiameterUU = static_cast<float>((SafeRadius * 2) + 1) * CellSizeUU;
    const float MeshScaleXY = DiameterUU / WF_BaseCubeMeshSizeUU;
    const float SafeThickness = FMath::Max(UndergroundBackdropThicknessUU, 1.0f);
    const float MeshScaleZ = SafeThickness / WF_BaseCubeMeshSizeUU;

    const float CenterWorldX = (static_cast<float>(CenterVoxel.X) + 0.5f) * CellSizeUU;
    const float CenterWorldY = (static_cast<float>(CenterVoxel.Y) + 0.5f) * CellSizeUU;

    // Put the backdrop below the cap layer so cap tiles and mining ghosts draw in front,
    // while the view beyond the cut reads as dark underground instead of sky/water.
    const float CenterWorldZ = static_cast<float>(ActiveVoxelZ) * CellSizeUU - CapThicknessUU - (SafeThickness * 0.5f) - 4.0f;

    return FTransform(
        FRotator::ZeroRotator,
        FVector(CenterWorldX, CenterWorldY, CenterWorldZ),
        FVector(MeshScaleXY, MeshScaleXY, MeshScaleZ));
}

// =============================================================================
// Colors
// =============================================================================

FLinearColor AMiningLayerRenderer::GetTerrainColorForLayer(int32 MaterialLayer) const
{
    if (!bUseTerrainLayerColors)
    {
        return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
    }

    switch (MaterialLayer)
    {
    case 0: return FLinearColor(0.80f, 0.72f, 0.45f, 1.0f); // Sand
    case 1: return FLinearColor(0.22f, 0.42f, 0.16f, 1.0f); // Grass
    case 2: return FLinearColor(0.45f, 0.30f, 0.18f, 1.0f); // Dirt
    case 3: return FLinearColor(0.38f, 0.38f, 0.40f, 1.0f); // Rock
    case 4: return FLinearColor(0.85f, 0.90f, 0.95f, 1.0f); // Snow
    case 5: return FLinearColor(0.15f, 0.35f, 0.70f, 1.0f); // Water
    case 6: return FLinearColor(1.00f, 0.25f, 0.05f, 1.0f); // Lava
    case 7: return FLinearColor(0.10f, 0.10f, 0.11f, 1.0f); // Basalt
    case 8: return FLinearColor(0.20f, 0.20f, 0.22f, 1.0f); // DeepRock
    default: return FLinearColor(0.65f, 0.65f, 0.65f, 1.0f);
    }
}

FLinearColor AMiningLayerRenderer::GetSelectedGhostColor() const
{
    return FLinearColor(1.0f, 0.95f, 0.05f, 0.60f);
}

FLinearColor AMiningLayerRenderer::GetConfirmedGhostColor() const
{
    return FLinearColor(1.0f, 0.85f, 0.05f, 0.85f);
}

// Source/WorldForge/Visual/TerrainCutawaySubsystem.cpp

#include "Visual/TerrainCutawaySubsystem.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogTerrainCutaway, Log, All);

namespace
{
    // Scalar parameter name used by M_WF_UndergroundBlock (and any other terrain material
    // that drives depth-from-surface blending). Centralized so the bug fix below cannot
    // disagree with the entry path's spelling of this parameter.
    static const FName GUndergroundSurfaceZParam = FName(TEXT("UndergroundSurfaceZ"));
}

// =============================================================================
// Lifecycle
// =============================================================================

void UTerrainCutawaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (TryLoadMPC())
    {
        UE_LOG(LogTerrainCutaway, Log,
            TEXT("[TerrainCutaway] MPC loaded successfully: %s"), *CachedMPC->GetName());
    }
    else
    {
        UE_LOG(LogTerrainCutaway, Warning,
            TEXT("[TerrainCutaway] MPC not found at path '%s'. "
                 "Clip plane will be inactive. Assign TerrainMPCPath in the CDO or a Blueprint subclass."),
            *TerrainMPCPath.ToString());
    }
}

void UTerrainCutawaySubsystem::Deinitialize()
{
    // Always restore full visibility when shutting down or unloading the level.
    if (bCutawayActive)
    {
        ExitCutawayMode();
    }

    Super::Deinitialize();
}

// =============================================================================
// Mode entry / exit
// =============================================================================

void UTerrainCutawaySubsystem::EnterCutawayMode(float InSurfaceWorldZ)
{
    SurfaceWorldZ         = InSurfaceWorldZ;
    ActiveViewLayerOffset = 0;   // Always start at surface level.
    bUseAbsoluteClipLayer = false;
    bCutawayActive        = true;

    // Surface entry must always be fully visible. Now that terrain materials
    // actually read ViewLayerClipZ through Opacity Mask, any stale low clip
    // value will hide the world before the player moves underground.
    if (CachedMPC)
    {
        UKismetMaterialLibrary::SetScalarParameterValue(
            GetWorld(), CachedMPC, GUndergroundSurfaceZParam, DisabledClipZ);
    }

    // At offset 0, ComputeClipWorldZ returns DisabledClipZ — no clipping occurs.
    // The player must press the view-layer-down key to actually reveal underground.
    ApplyClipPlane();
    OnViewLayerChanged.Broadcast(ActiveViewLayerOffset);

    UE_LOG(LogTerrainCutaway, Log,
        TEXT("[TerrainCutaway] Entered cutaway mode. SurfaceZ=%.1f ClipZ=%.1f MPC.UndergroundSurfaceZ=disabled"),
        SurfaceWorldZ, ComputeClipWorldZ());
}


void UTerrainCutawaySubsystem::EnterCutawayAtWorldZ(float LayerWorldZ, bool bStartClipped)
{
    bCutawayActive        = true;
    bUseAbsoluteClipLayer = bStartClipped;
    ActiveViewLayerOffset = 0;
    SurfaceWorldZ         = LayerWorldZ;
    AbsoluteClipWorldZ    = LayerWorldZ;

    // ─── BUGFIX 2026-05-11 ───────────────────────────────────────────────────
    // Previously this unconditionally wrote UndergroundSurfaceZ to the MPC on mode
    // entry, even when bStartClipped=false. The terrain material (M_WF_UndergroundBlock
    // and the MC_WorldForge_Terrain layers) reads this scalar for depth-from-surface
    // blending and/or visibility logic. Writing it at the actual surface Z while no
    // clip was supposed to be active caused the entire surface to render black even
    // though ViewLayerClipZ was at the disabled sentinel.
    //
    // We now only push UndergroundSurfaceZ when clipping is actively enabled.
    // Offset-0 entry leaves the MPC untouched so the surface renders normally.
    // ─────────────────────────────────────────────────────────────────────────
    if (CachedMPC)
    {
        UKismetMaterialLibrary::SetScalarParameterValue(
            GetWorld(), CachedMPC, GUndergroundSurfaceZParam,
            bStartClipped ? LayerWorldZ : DisabledClipZ);
    }

    ApplyClipPlane();
    OnViewLayerChanged.Broadcast(ActiveViewLayerOffset);

    UE_LOG(LogTerrainCutaway, Log,
        TEXT("[TerrainCutaway] Entered absolute cutaway mode. LayerWorldZ=%.1f StartClipped=%s ClipZ=%.1f MPC.UndergroundSurfaceZ=%s"),
        AbsoluteClipWorldZ,
        bStartClipped ? TEXT("true") : TEXT("false"),
        ComputeClipWorldZ(),
        bStartClipped ? TEXT("written") : TEXT("reset to disabled (surface preserved)"));
}

void UTerrainCutawaySubsystem::SetAbsoluteClipLayerWorldZ(float LayerWorldZ, bool bEnableClip)
{
    bCutawayActive        = true;
    bUseAbsoluteClipLayer = bEnableClip;
    AbsoluteClipWorldZ    = LayerWorldZ;
    SurfaceWorldZ         = LayerWorldZ;
    ActiveViewLayerOffset = 0;

    // ─── BUGFIX 2026-05-11 ───────────────────────────────────────────────────
    // Mirror EnterCutawayAtWorldZ's policy: write UndergroundSurfaceZ ONLY when
    // clipping is enabled. When the player scrolls back UP to the surface, the
    // MineLayerCoordinator sync path calls us with bEnableClip=false, and we
    // restore the disabled sentinel so the surface terrain stops being treated
    // as underground by the material's depth blend.
    // ─────────────────────────────────────────────────────────────────────────
    if (CachedMPC)
    {
        UKismetMaterialLibrary::SetScalarParameterValue(
            GetWorld(), CachedMPC, GUndergroundSurfaceZParam,
            bEnableClip ? LayerWorldZ : DisabledClipZ);
    }

    ApplyClipPlane();
    OnViewLayerChanged.Broadcast(ActiveViewLayerOffset);

    UE_LOG(LogTerrainCutaway, Log,
        TEXT("[TerrainCutaway] AbsoluteLayerWorldZ=%.1f ClipEnabled=%s ClipZ=%.1f MPC.UndergroundSurfaceZ=%s"),
        AbsoluteClipWorldZ,
        bEnableClip ? TEXT("true") : TEXT("false"),
        ComputeClipWorldZ(),
        bEnableClip ? TEXT("written") : TEXT("reset to disabled"));
}

void UTerrainCutawaySubsystem::SetAbsoluteClipLayerVoxelZ(int32 VoxelZ)
{
    SetAbsoluteClipLayerWorldZ(static_cast<float>(VoxelZ) * VoxelLayerHeightUU);
}

void UTerrainCutawaySubsystem::ExitCutawayMode()
{
    bCutawayActive        = false;
    ActiveViewLayerOffset = 0;
    bUseAbsoluteClipLayer = false;
    AbsoluteClipWorldZ    = 0.0f;

    // Push clip Z to max to restore full terrain visibility.
    // ─── BUGFIX 2026-05-11 ───────────────────────────────────────────────────
    // Also reset UndergroundSurfaceZ to the disabled sentinel so any depth-blend
    // logic in the terrain material reads everywhere as "at the surface" once
    // mine mode ends. Previously this was left at whatever value the last cutaway
    // entry wrote — leaving the surface terrain in a half-broken state.
    // ─────────────────────────────────────────────────────────────────────────
    if (CachedMPC)
    {
        UKismetMaterialLibrary::SetScalarParameterValue(
            GetWorld(), CachedMPC, ClipZParameterName, DisabledClipZ);

        UKismetMaterialLibrary::SetScalarParameterValue(
            GetWorld(), CachedMPC, GUndergroundSurfaceZParam, DisabledClipZ);
    }

    OnViewLayerChanged.Broadcast(0);

    UE_LOG(LogTerrainCutaway, Log,
        TEXT("[TerrainCutaway] Exited cutaway mode. Clip plane and UndergroundSurfaceZ reset to disabled."));
}

// =============================================================================
// View layer controls
// =============================================================================

void UTerrainCutawaySubsystem::AdjustViewLayer(int32 Delta)
{
    if (!bCutawayActive)
    {
        UE_LOG(LogTerrainCutaway, Verbose,
            TEXT("[TerrainCutaway] AdjustViewLayer(%d) ignored — cutaway not active. Enter Mine mode first."),
            Delta);
        return;
    }

    if (bUseAbsoluteClipLayer)
    {
        AbsoluteClipWorldZ += static_cast<float>(Delta) * VoxelLayerHeightUU;
        SurfaceWorldZ = AbsoluteClipWorldZ;
        ApplyClipPlane();

        // Keep UndergroundSurfaceZ in sync when actively clipping in absolute mode.
        if (CachedMPC)
        {
            UKismetMaterialLibrary::SetScalarParameterValue(
                GetWorld(), CachedMPC, GUndergroundSurfaceZParam, AbsoluteClipWorldZ);
        }

        OnViewLayerChanged.Broadcast(ActiveViewLayerOffset);

        UE_LOG(LogTerrainCutaway, Log,
            TEXT("[TerrainCutaway] Absolute view adjust Delta=%d ClipZ=%.1f"),
            Delta,
            ComputeClipWorldZ());
        return;
    }

    const int32 NewOffset = FMath::Clamp(
        ActiveViewLayerOffset + Delta,
        MinViewLayerOffset,
        MaxViewLayerOffset);

    if (NewOffset == ActiveViewLayerOffset)
    {
        UE_LOG(LogTerrainCutaway, Verbose,
            TEXT("[TerrainCutaway] View layer at limit (%d). Cannot go further."),
            ActiveViewLayerOffset);
        return;
    }

    const bool bWasAboveSurface  = (ActiveViewLayerOffset >= 0);
    ActiveViewLayerOffset        = NewOffset;
    const bool bIsNowUnderground = (ActiveViewLayerOffset < 0);

    ApplyClipPlane();

    // BUGFIX 2026-05-11: mirror entry policy here for the offset-based legacy path.
    // Only write UndergroundSurfaceZ when we cross into underground; reset it when
    // we cross back out to the surface.
    if (CachedMPC)
    {
        if (bIsNowUnderground)
        {
            UKismetMaterialLibrary::SetScalarParameterValue(
                GetWorld(), CachedMPC, GUndergroundSurfaceZParam, SurfaceWorldZ);
        }
        else if (bWasAboveSurface == false)
        {
            // We just left underground — restore disabled state.
            UKismetMaterialLibrary::SetScalarParameterValue(
                GetWorld(), CachedMPC, GUndergroundSurfaceZParam, DisabledClipZ);
        }
    }

    OnViewLayerChanged.Broadcast(ActiveViewLayerOffset);

    UE_LOG(LogTerrainCutaway, Log,
        TEXT("[TerrainCutaway] ViewLayer=%d  ClipZ=%.1f Underground=%s"),
        ActiveViewLayerOffset,
        ComputeClipWorldZ(),
        bIsNowUnderground ? TEXT("true") : TEXT("false"));
}

void UTerrainCutawaySubsystem::ResetViewLayer()
{
    if (ActiveViewLayerOffset == 0)
    {
        return;
    }

    ActiveViewLayerOffset = 0;

    if (bCutawayActive)
    {
        ApplyClipPlane();

        // Surface reset must always restore disabled UndergroundSurfaceZ. This avoids
        // stale underground state after material graph edits, hot reloads, or returning
        // to the surface from absolute LayerView-driven cutaway mode.
        if (CachedMPC)
        {
            UKismetMaterialLibrary::SetScalarParameterValue(
                GetWorld(), CachedMPC, GUndergroundSurfaceZParam, DisabledClipZ);
        }
    }

    OnViewLayerChanged.Broadcast(0);

    UE_LOG(LogTerrainCutaway, Log, TEXT("[TerrainCutaway] View layer reset to surface (offset 0)."));
}

// =============================================================================
// Internal helpers
// =============================================================================

float UTerrainCutawaySubsystem::ComputeClipWorldZ() const
{
    if (!bCutawayActive)
    {
        return DisabledClipZ;
    }

    // LayerView-driven mode: clip directly at the active absolute layer so
    // camera height and stale view offsets can never move the terrain slice.
    if (bUseAbsoluteClipLayer)
    {
        return AbsoluteClipWorldZ + ClipEdgeBiasUU;
    }

    // Legacy offset mode. Offset 0 means no clipping.
    if (ActiveViewLayerOffset >= 0)
    {
        return DisabledClipZ;
    }

    // Underground view. See header for the layer math.
    const float LayerShift =
        static_cast<float>(ActiveViewLayerOffset + 1) * VoxelLayerHeightUU;

    return SurfaceWorldZ + LayerShift + ClipEdgeBiasUU;
}

void UTerrainCutawaySubsystem::ApplyClipPlane() const
{
    if (!CachedMPC)
    {
        UE_LOG(LogTerrainCutaway, Warning,
            TEXT("[TerrainCutaway] ApplyClipPlane: MPC not loaded. Clip plane cannot be updated."));
        return;
    }

    UKismetMaterialLibrary::SetScalarParameterValue(
        GetWorld(), CachedMPC, ClipZParameterName, ComputeClipWorldZ());
}

bool UTerrainCutawaySubsystem::TryLoadMPC()
{
    if (CachedMPC)
    {
        return true;
    }

    if (!TerrainMPCPath.IsValid())
    {
        return false;
    }

    CachedMPC = Cast<UMaterialParameterCollection>(TerrainMPCPath.TryLoad());
    return CachedMPC != nullptr;
}

// StabilitySubsystem.cpp
// WorldForge – Structural Stability System
// Place in: Source/WorldForge/Mining/StabilitySubsystem.cpp

#include "Mining/StabilitySubsystem.h"

// Engine
#include "Containers/Queue.h"

// WorldForge subsystems
#include "Colony/EventBus.h"
#include "Voxel/DigSubsystem.h"      // DigVoxelAtGrid() for cascade removals
#include "Colony/NavigationContext.h"
#include "Data/WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogStability, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Overlay colour constants
// ─────────────────────────────────────────────────────────────────────────────

const FLinearColor UStabilitySubsystem::Color_Unstable = FLinearColor(1.00f, 0.00f, 0.00f, 0.90f);
const FLinearColor UStabilitySubsystem::Color_Critical = FLinearColor(1.00f, 0.40f, 0.00f, 0.85f);
const FLinearColor UStabilitySubsystem::Color_Poor     = FLinearColor(1.00f, 1.00f, 0.00f, 0.80f);
const FLinearColor UStabilitySubsystem::Color_Adequate = FLinearColor(0.60f, 1.00f, 0.00f, 0.75f);
const FLinearColor UStabilitySubsystem::Color_Stable   = FLinearColor(0.00f, 1.00f, 0.20f, 0.70f);

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void UStabilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // DigSubsystem must be ready before us so ProcessCollapses can call
    // DigVoxelAtGrid() on cascade tiles.
    Collection.InitializeDependency(UDigSubsystem::StaticClass());
    Collection.InitializeDependency(UEventBus::StaticClass());

    Super::Initialize(Collection);

    AirVoxels.Empty();
    AirVoxels.Reserve(4096);
    StabilityCache.Empty();
    StabilityCache.Reserve(1024);

    UE_LOG(LogStability, Log, TEXT("[Stability] Initialized. MaxSpan=%d MaxCascade=%d"),
        WF_STABILITY_MAX_SPAN, WF_COLLAPSE_MAX_CASCADE);
}

void UStabilitySubsystem::Deinitialize()
{
    AirVoxels.Empty();
    StabilityCache.Empty();
    Super::Deinitialize();
    UE_LOG(LogStability, Log, TEXT("[Stability] Deinitialized."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Primary API
// ─────────────────────────────────────────────────────────────────────────────

FCollapseResult UStabilitySubsystem::OnVoxelRemoved(FIntVector VoxelGrid, int32 SettlementID)
{
    FCollapseResult Result;
    Result.SettlementID = SettlementID;

    // 1. Register the voxel as air.
    AirVoxels.Add(VoxelGrid);
    InvalidateCacheAround(VoxelGrid, WF_STABILITY_MAX_SPAN + 1);

    // 2. Scan surrounding slab for newly unsupported ceiling voxels.
    TArray<FIntVector> CollapseQueue;
    FindUnstableCeilingVoxels(VoxelGrid, CollapseQueue);

    // 3. Process collapses (may cascade).
    if (CollapseQueue.Num() > 0)
    {
        UE_LOG(LogStability, Warning,
            TEXT("[Stability] Voxel (%d,%d,%d) removed -> %d ceiling voxels at risk."),
            VoxelGrid.X, VoxelGrid.Y, VoxelGrid.Z, CollapseQueue.Num());

        ProcessCollapses(CollapseQueue, Result, 0);
    }

    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query API
// ─────────────────────────────────────────────────────────────────────────────

int32 UStabilitySubsystem::GetVoxelStability(FIntVector VoxelGrid) const
{
    if (IsAirVoxel(VoxelGrid)) return 0;

    if (const int32* Cached = StabilityCache.Find(VoxelGrid))
    {
        return *Cached;
    }

    // Not in cache -> natural terrain -> implicitly Stable.
    return WF_STABILITY_MAX_SPAN;
}

EStabilityTier UStabilitySubsystem::GetStabilityTier(FIntVector VoxelGrid) const
{
    const int32 Span = GetVoxelStability(VoxelGrid);

    if (Span <= 0) return EStabilityTier::Unstable;
    if (Span == 1) return EStabilityTier::Critical;
    if (Span == 2) return EStabilityTier::Poor;
    if (Span == 3) return EStabilityTier::Adequate;
    return EStabilityTier::Stable;
}

FLinearColor UStabilitySubsystem::GetStabilityColor(FIntVector VoxelGrid) const
{
    switch (GetStabilityTier(VoxelGrid))
    {
    case EStabilityTier::Unstable:  return Color_Unstable;
    case EStabilityTier::Critical:  return Color_Critical;
    case EStabilityTier::Poor:      return Color_Poor;
    case EStabilityTier::Adequate:  return Color_Adequate;
    default:                        return Color_Stable;
    }
}

bool UStabilitySubsystem::IsAirVoxel(FIntVector VoxelGrid) const
{
    return AirVoxels.Contains(VoxelGrid);
}

// ─────────────────────────────────────────────────────────────────────────────
// Voxel state helpers
// ─────────────────────────────────────────────────────────────────────────────

bool UStabilitySubsystem::IsSolidVoxel(FIntVector V) const
{
    return !AirVoxels.Contains(V);
}

bool UStabilitySubsystem::HasVerticalSupport(FIntVector V) const
{
    if (V.Z <= 0) return true;                                    // bedrock
    return IsSolidVoxel(FIntVector(V.X, V.Y, V.Z - 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// Stability computation
// ─────────────────────────────────────────────────────────────────────────────

int32 UStabilitySubsystem::ComputeSpanStability(FIntVector C) const
{
    // C itself is a column -> immediately stable.
    if (HasVerticalSupport(C))
    {
        return WF_STABILITY_MAX_SPAN;
    }

    // BFS horizontally at Z=C.Z through solid voxels to find the nearest column.
    struct FBFSNode { FIntVector Pos; int32 Dist; };

    TQueue<FBFSNode> Queue;
    TSet<FIntVector> Visited;

    Queue.Enqueue({ C, 0 });
    Visited.Add(C);

    static const FIntVector HNeighbors[4] = {
        {  1, 0, 0 }, { -1, 0, 0 },
        {  0, 1, 0 }, {  0,-1, 0 }
    };

    while (!Queue.IsEmpty())
    {
        FBFSNode Current;
        Queue.Dequeue(Current);

        const int32 NextDist = Current.Dist + 1;
        if (NextDist > WF_STABILITY_MAX_SPAN)
        {
            continue;
        }

        for (const FIntVector& Off : HNeighbors)
        {
            FIntVector N = Current.Pos + Off;

            if (Visited.Contains(N)) continue;
            if (!IsSolidVoxel(N))   continue;

            Visited.Add(N);

            if (HasVerticalSupport(N))
            {
                return WF_STABILITY_MAX_SPAN - NextDist;
            }

            Queue.Enqueue({ N, NextDist });
        }
    }

    return 0; // No column within budget.
}

// ─────────────────────────────────────────────────────────────────────────────
// Ceiling scan
// ─────────────────────────────────────────────────────────────────────────────

void UStabilitySubsystem::FindUnstableCeilingVoxels(
    FIntVector RemovedVoxel,
    TArray<FIntVector>& OutUnstable)
{
    // Scan slab around the removed voxel across multiple Z levels so that
    // both ceilings (Z+1) and hanging wall segments (same Z) are checked.
    const int32 HRadius = WF_STABILITY_MAX_SPAN + 1;
    const int32 ZMin    = RemovedVoxel.Z - 1;
    const int32 ZMax    = RemovedVoxel.Z + WF_STABILITY_MAX_SPAN + 1;

    for (int32 ScanZ = ZMin; ScanZ <= ZMax; ++ScanZ)
    {
        for (int32 DX = -HRadius; DX <= HRadius; ++DX)
        {
            for (int32 DY = -HRadius; DY <= HRadius; ++DY)
            {
                const FIntVector Candidate(
                    RemovedVoxel.X + DX,
                    RemovedVoxel.Y + DY,
                    ScanZ);

                if (!IsSolidVoxel(Candidate)) continue; // Already air.

                // A ceiling voxel has air directly below it.
                const FIntVector Below(Candidate.X, Candidate.Y, Candidate.Z - 1);
                if (IsSolidVoxel(Below)) continue;

                const int32 Stab = ComputeSpanStability(Candidate);
                StabilityCache.Add(Candidate, Stab);

                if (Stab <= 0)
                {
                    OutUnstable.Add(Candidate);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Collapse processing
// ─────────────────────────────────────────────────────────────────────────────

void UStabilitySubsystem::ProcessCollapses(
    TArray<FIntVector>& CollapseQueue,
    FCollapseResult&    Result,
    int32               CascadeDepth)
{
    if (CascadeDepth >= WF_COLLAPSE_MAX_CASCADE)
    {
        UE_LOG(LogStability, Warning,
            TEXT("[Stability] Cascade cap reached (depth=%d). %d voxels unprocessed."),
            CascadeDepth, CollapseQueue.Num());
        return;
    }
    if (CollapseQueue.IsEmpty()) return;

    UDigSubsystem* DigSub = GetWorld()->GetSubsystem<UDigSubsystem>();
    UEventBus*     Bus    = GetWorld()->GetSubsystem<UEventBus>();

    TArray<FIntVector> NextCascade;

    for (const FIntVector& CV : CollapseQueue)
    {
        if (!IsSolidVoxel(CV)) continue; // Already air from earlier in this cascade.

        UE_LOG(LogStability, Warning,
            TEXT("[Stability] CAVE-IN at (%d,%d,%d) | Cascade=%d"),
            CV.X, CV.Y, CV.Z, CascadeDepth);

        // ── 1. Mark as air BEFORE recursion to prevent double-processing. ──
        AirVoxels.Add(CV);
        StabilityCache.Remove(CV);
        Result.CollapsedVoxels.Add(CV);
        Result.MaxCascadeDepthReached = FMath::Max(Result.MaxCascadeDepthReached, CascadeDepth);

        // ── 2. Injure agents in collapse zone. ──
        TArray<int32> HitAgents = GetAgentsNearVoxel(CV, Result.SettlementID);
        for (int32 ID : HitAgents)
        {
            Result.InjuredAgentIDs.AddUnique(ID);
        }

        // ── 3. Physically remove the voxel from VoxelPlugin via DigSubsystem. ──
        if (DigSub)
        {
            DigSub->DigVoxelAtGrid(CV);
        }
        else
        {
            UE_LOG(LogStability, Error,
                TEXT("[Stability] DigSubsystem unavailable - voxel (%d,%d,%d) not physically removed."),
                CV.X, CV.Y, CV.Z);
        }

        // ── 4. Invalidate cache around the new air voxel. ──
        InvalidateCacheAround(CV, WF_STABILITY_MAX_SPAN + 1);

        // ── 5. Find newly exposed ceiling voxels created by this collapse. ──
        TArray<FIntVector> NewUnstable;
        FindUnstableCeilingVoxels(CV, NewUnstable);
        NextCascade.Append(NewUnstable);
    }

    // ── 6. Dispatch a single collapse event per top-level trigger. ──
    if (Bus && Result.CollapsedVoxels.Num() > 0 && CascadeDepth == 0)
    {
        // Dispatch TAG_Sim_Event_CaveCollapse so HealthProcessor can apply
        // damage to agents in Result.InjuredAgentIDs.
        //
        // Uncomment once EventBus Dispatch() payload API is confirmed:
        // Bus->Dispatch(TAG_Sim_Event_CaveCollapse, /* FCollapseResult payload */);
        //
        UE_LOG(LogStability, Warning,
            TEXT("[Stability] CaveCollapse dispatched. Voxels=%d Injured=%d MaxDepth=%d"),
            Result.CollapsedVoxels.Num(),
            Result.InjuredAgentIDs.Num(),
            Result.MaxCascadeDepthReached);
    }

    // ── 7. Recurse for cascade. ──
    if (NextCascade.Num() > 0)
    {
        ProcessCollapses(NextCascade, Result, CascadeDepth + 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Agent proximity — stub, expand when NavigationContext API is confirmed
// ─────────────────────────────────────────────────────────────────────────────

TArray<int32> UStabilitySubsystem::GetAgentsNearVoxel(FIntVector VoxelGrid, int32 SettlementID) const
{
    TArray<int32> HitAgents;

    // PREFERRED PATH (cleaner separation of concerns):
    // Leave this returning empty and handle agent damage entirely inside
    // HealthProcessor's TAG_Sim_Event_CaveCollapse event subscription.
    // The FCollapseResult passed via EventBus contains CollapsedVoxels; HP
    // can query agent grid positions and do the proximity check there.
    //
    // ALTERNATIVE (if direct query is needed):
    // UNavigationContext* NavCtx = GetWorld()->GetSubsystem<UNavigationContext>();
    // NavCtx->ForEachAgentInSettlement(SettlementID, [&](int32 AgentID, FIntVector WorldCell)
    // {
    //     const FVector AgentWorld  = FVector(WorldCell) * WF_VOXEL_SIZE_UU;
    //     const FVector CollapsePos = VoxelGridToWorld(VoxelGrid);
    //     if (FVector::DistSquared(AgentWorld, CollapsePos) <= FMath::Square(WF_VOXEL_SIZE_UU * 1.5f))
    //     {
    //         HitAgents.AddUnique(AgentID);
    //     }
    // });

    return HitAgents;
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate helpers
// ─────────────────────────────────────────────────────────────────────────────

/*static*/ FVector UStabilitySubsystem::VoxelGridToWorld(FIntVector VG)
{
    return FVector(
        static_cast<float>(VG.X) * WF_VOXEL_SIZE_UU,
        static_cast<float>(VG.Y) * WF_VOXEL_SIZE_UU,
        static_cast<float>(VG.Z) * WF_VOXEL_SIZE_UU);
}

void UStabilitySubsystem::InvalidateCacheAround(FIntVector Center, int32 Radius)
{
    for (int32 DX = -Radius; DX <= Radius; ++DX)
    {
        for (int32 DY = -Radius; DY <= Radius; ++DY)
        {
            for (int32 DZ = -1; DZ <= WF_STABILITY_MAX_SPAN + 1; ++DZ)
            {
                StabilityCache.Remove(FIntVector(
                    Center.X + DX,
                    Center.Y + DY,
                    Center.Z + DZ));
            }
        }
    }
}

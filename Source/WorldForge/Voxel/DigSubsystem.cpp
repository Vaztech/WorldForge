// DigSubsystem.cpp
// WorldForge — Phase 1: Digging Foundation
//
// IMPORTANT:
// This version does NOT call private VoxelWorldManager brush helpers directly.
// DigSubsystem now uses the public voxel edit API:
//
//   RemoveBlockAt(GridPos)
//   RestoreBlockAt(GridPos, BlockType)
//   GetBlockTypeAt(GridPos)
//
// This matches the current VoxelWorldManager.h architecture.

#include "Voxel/DigSubsystem.h"
#include "Voxel/VoxelWorldManager.h"
#include "Colony/EventBus.h"
#include "Data/WorldForgeTags.h"
#include "Mining/ResourceSpawnSubsystem.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

// ===================================================================
// USubsystem lifecycle
// ===================================================================

bool UDigSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	return World->IsGameWorld();
}

void UDigSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log,
		TEXT("[DigSubsystem] Initialized using public VoxelWorldManager dig API."));
}

void UDigSubsystem::Deinitialize()
{
	CachedVoxelManager = nullptr;
	CachedEventBus = nullptr;
	CachedResourceSpawn = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[DigSubsystem] Deinitialized."));

	Super::Deinitialize();
}

// ===================================================================
// Primary API
// ===================================================================

FDigResult UDigSubsystem::DigAt(const FDigParams& Params)
{
	FDigResult Result;
	Result.DigOrigin = Params.TargetGridPos;
	Result.DigRadius = Params.Radius;
	Result.BrushShape = Params.BrushShape;

	UVoxelWorldManager* VoxelMgr = GetVoxelManager();
	if (!VoxelMgr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] DigAt failed — VoxelWorldManager not available."));

		return Result;
	}

	// ------------------------------------------------------------------
	// Step 1:
	// Compute brush metadata for debug drawing and result tracking.
	//
	// We are NOT applying this brush directly here.
	// VoxelWorldManager owns the private SDF/runtime edit implementation.
	// ------------------------------------------------------------------
	FDigBrushRecord BrushRecord;
	if (!ComputeBrushParameters(Params, BrushRecord))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] DigAt(%s) — failed to compute brush parameters."),
			*Params.TargetGridPos.ToString());

		return Result;
	}

	// BrushID is intentionally INDEX_NONE because DigSubsystem is not
	// directly creating/removing SDF brush records anymore.
	BrushRecord.BrushID = INDEX_NONE;

	// ------------------------------------------------------------------
	// Step 2:
	// Sample blocks in the volume before removing anything.
	//
	// Each FRemovedBlockEntry stores:
	// - GridPosition
	// - BlockType
	//
	// This is also what UndoDig uses to restore removed blocks.
	// ------------------------------------------------------------------
	TArray<FRemovedBlockEntry> SampledBlocks = SampleBlocksInVolume(Params);

	if (SampledBlocks.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[DigSubsystem] DigAt(%s) — no solid blocks in brush volume."),
			*Params.TargetGridPos.ToString());

		return Result;
	}

	// ------------------------------------------------------------------
	// Step 3:
	// Remove each sampled block through VoxelWorldManager's public API.
	//
	// This avoids illegal access to private functions like:
	// - MakeDigBrush()
	// - ApplyRuntimeBrush()
	// - RemoveBrushByID()
	// ------------------------------------------------------------------
	TArray<FRemovedBlockEntry> ActuallyRemovedBlocks;
	ActuallyRemovedBlocks.Reserve(SampledBlocks.Num());

	for (const FRemovedBlockEntry& Entry : SampledBlocks)
	{
		const bool bRemoved = VoxelMgr->RemoveBlockAt(Entry.GridPosition);

		if (bRemoved)
		{
			ActuallyRemovedBlocks.Add(Entry);
		}
		else
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[DigSubsystem] RemoveBlockAt skipped/failed at %s type=%s"),
				*Entry.GridPosition.ToString(),
				*Entry.BlockType.ToString());
		}
	}

	if (ActuallyRemovedBlocks.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[DigSubsystem] DigAt(%s) — sampled blocks existed, but none were removed."),
			*Params.TargetGridPos.ToString());

		return Result;
	}

	// ------------------------------------------------------------------
	// Step 4:
	// Populate result.
	// ------------------------------------------------------------------
	Result.BrushRecord = BrushRecord;

	// Public API mode does not get a real private SDF BrushID from VoxelWorldManager.
	// Use a stable pseudo ID so code that checks BrushID != INDEX_NONE does not
	// incorrectly treat a successful public-API dig as "no effect".
	Result.BrushRecord.BrushID = NextDigResultID++;

	Result.RemovedBlocks = MoveTemp(ActuallyRemovedBlocks);
	Result.DepthReached = CalculateMaxDepthReached(Result.RemovedBlocks);

	TArray<FIntVector> DugPositions;
	DugPositions.Reserve(Result.RemovedBlocks.Num());

	for (const FRemovedBlockEntry& Entry : Result.RemovedBlocks)
	{
		DugPositions.Add(Entry.GridPosition);
	}

	Result.RevealedNodes = CheckForNodeReveals(DugPositions, Result.DepthReached);

	// Trace logging.
	for (const FRemovedBlockEntry& Entry : Result.RemovedBlocks)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigTrace] Target=%s Grid=%s Type=%s Shape=%d Depth=%d"),
			*Params.TargetGridPos.ToString(),
			*Entry.GridPosition.ToString(),
			*Entry.BlockType.ToString(),
			static_cast<int32>(Params.BrushShape),
			Result.DepthReached);
	}

	// ------------------------------------------------------------------
	// Step 5:
	// Dispatch dig result through EventBus.
	// ------------------------------------------------------------------
	if (Result.WasEffective())
	{
		BroadcastDigResult(Result);

		UE_LOG(LogTemp, Log,
			TEXT("[DigSubsystem] Dug %d block(s) at %s via %s brush, depth=%d, nodes=%d"),
			Result.RemovedBlocks.Num(),
			*Params.TargetGridPos.ToString(),
			*UEnum::GetValueAsString(Params.BrushShape),
			Result.DepthReached,
			Result.RevealedNodes.Num());
	}

#if !UE_BUILD_SHIPPING
	// Debug visualization: draw the intended brush volume.
	if (UWorld* World = GetWorld())
	{
		switch (Params.BrushShape)
		{
		case EDigBrushShape::Sphere:
			DrawDebugSphere(
				World,
				BrushRecord.WorldCenter,
				BrushRecord.Radius,
				16,
				FColor::Cyan,
				false,
				5.0f);
			break;

		case EDigBrushShape::Cylinder:
			DrawDebugCylinder(
				World,
				BrushRecord.WorldCenter - FVector(0.0f, 0.0f, BrushRecord.HalfExtents.Z),
				BrushRecord.WorldCenter + FVector(0.0f, 0.0f, BrushRecord.HalfExtents.Z),
				BrushRecord.Radius,
				16,
				FColor::Cyan,
				false,
				5.0f);
			break;

		case EDigBrushShape::Box:
			DrawDebugBox(
				World,
				BrushRecord.WorldCenter,
				BrushRecord.HalfExtents,
				FColor::Cyan,
				false,
				5.0f);
			break;
		}
	}
#endif

	return Result;
}

bool UDigSubsystem::UndoDig(const FDigResult& Result)
{
	UVoxelWorldManager* VoxelMgr = GetVoxelManager();
	if (!VoxelMgr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] UndoDig failed — VoxelWorldManager not available."));

		return false;
	}

	if (Result.RemovedBlocks.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] UndoDig — no removed blocks to restore."));

		return false;
	}

	// ------------------------------------------------------------------
	// Restore every block that DigAt actually removed.
	//
	// This is safer than relying on BrushID because the current public
	// VoxelWorldManager API restores blocks by grid position + block type.
	// ------------------------------------------------------------------
	int32 RestoredCount = 0;

	for (const FRemovedBlockEntry& Entry : Result.RemovedBlocks)
	{
		const bool bRestored = VoxelMgr->RestoreBlockAt(
			Entry.GridPosition,
			Entry.BlockType);

		if (bRestored)
		{
			++RestoredCount;
		}
		else
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[DigSubsystem] UndoDig — failed to restore %s type=%s"),
				*Entry.GridPosition.ToString(),
				*Entry.BlockType.ToString());
		}
	}

	if (RestoredCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] UndoDig — restore attempted, but no blocks were restored."));

		return false;
	}

	// TODO Phase 2+:
	// If the dig revealed resource nodes, un-reveal them here.
	//
	// UResourceNodeSubsystem* NodeSub = GetWorld()->GetSubsystem<UResourceNodeSubsystem>();
	// if (NodeSub)
	// {
	//     for (const FRevealedNodeEntry& Node : Result.RevealedNodes)
	//     {
	//         NodeSub->HideNode(Node.NodeID);
	//     }
	// }

	UE_LOG(LogTemp, Log,
		TEXT("[DigSubsystem] UndoDig — restored %d/%d block(s) at origin %s."),
		RestoredCount,
		Result.RemovedBlocks.Num(),
		*Result.DigOrigin.ToString());

	return RestoredCount > 0;
}

// ===================================================================
// Queries
// ===================================================================

bool UDigSubsystem::CanDigAt(const FIntVector& GridPos) const
{
	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return false;
	}

	if (!VoxelMgr->IsValidGridPosition(GridPos))
	{
		return false;
	}

	// First allow direct underground solid targets.
	const FGameplayTag DirectBlockType = VoxelMgr->GetBlockTypeAt(GridPos);
	if (DirectBlockType.IsValid() && DirectBlockType != TAG_Terrain_Bedrock)
	{
		return true;
	}

	// Fallback: if the player clicked near the surface/air, resolve to a
	// nearby surface block. This preserves slope/surface digging behavior.
	FIntVector SurfaceBlock;
	if (!VoxelMgr->TryFindSurfaceBlockNearZ(
		FIntPoint(GridPos.X, GridPos.Y),
		GridPos.Z,
		18,
		SurfaceBlock))
	{
		return false;
	}

	const FGameplayTag SurfaceBlockType = VoxelMgr->GetBlockTypeAt(SurfaceBlock);

	return SurfaceBlockType.IsValid() && SurfaceBlockType != TAG_Terrain_Bedrock;
}

int32 UDigSubsystem::GetSurfaceDepthAt(const FIntPoint& GridXY) const
{
	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return 0;
	}

	return VoxelMgr->GetTerrainDepthAt(GridXY);
}

int32 UDigSubsystem::GetRelativeDepth(const FIntVector& GridPos) const
{
	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return 0;
	}

	const int32 SurfaceZ = VoxelMgr->GetSurfaceZAt(FIntPoint(GridPos.X, GridPos.Y));

	return SurfaceZ - GridPos.Z;
}

// ===================================================================
// Brush computation
// ===================================================================

bool UDigSubsystem::ComputeBrushParameters(
	const FDigParams& Params,
	FDigBrushRecord& OutRecord) const
{
	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return false;
	}

	const float VoxelSize = VoxelMgr->GetRenderVoxelSize();
	if (VoxelSize <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DigSubsystem] ComputeBrushParameters — invalid VoxelSize (%.2f)."),
			VoxelSize);

		return false;
	}

	// ------------------------------------------------------------------
	// Match SampleBlocksInVolume behavior:
	//
	// If the raw target is already a solid block, treat it as an underground
	// cave/tunnel dig and keep the exact target Z.
	//
	// If the raw target is air/invalid, fall back to nearby surface snapping.
	// ------------------------------------------------------------------

	const FGameplayTag TargetBlockType = VoxelMgr->GetBlockTypeAt(Params.TargetGridPos);
	const bool bTargetIsSolid =
		TargetBlockType.IsValid() &&
		TargetBlockType != TAG_Terrain_Bedrock;

	FIntVector BrushAnchor = Params.TargetGridPos;

	if (!bTargetIsSolid)
	{
		if (!VoxelMgr->TryFindSurfaceBlockNearZ(
			FIntPoint(Params.TargetGridPos.X, Params.TargetGridPos.Y),
			Params.TargetGridPos.Z,
			18,
			BrushAnchor))
		{
			BrushAnchor = Params.TargetGridPos;
		}
	}

	// Offset the brush center downward by half the action depth so the
	// debug volume shows the area being carved into the terrain.
	const float DepthOffset =
		static_cast<float>(FMath::Max(Params.DepthPerAction, 1)) *
		VoxelSize *
		0.5f;

	FVector WorldCenter = VoxelMgr->GridToWorld(
		FIntPoint(BrushAnchor.X, BrushAnchor.Y),
		BrushAnchor.Z);

	WorldCenter.Z -= DepthOffset;

	const int32 R = FMath::Max(Params.Radius, 1);
	const int32 Depth = FMath::Max(Params.DepthPerAction, 1);

	// Radius=1 means one block footprint.
	// Radius=2 means 3x3 footprint.
	// Radius=R means (2R-1)x(2R-1).
	const float XYExtent = (static_cast<float>(R) - 0.5f + 0.225f) * VoxelSize;
	const float HalfDepth = (static_cast<float>(Depth) * 0.5f + 0.225f) * VoxelSize;

	const float Falloff = (Params.FalloffOverride > 0.0f)
		? Params.FalloffOverride
		: VoxelSize * 0.5f;

	OutRecord.Shape = Params.BrushShape;
	OutRecord.WorldCenter = WorldCenter;
	OutRecord.Falloff = Falloff;
	OutRecord.BrushID = INDEX_NONE;

	switch (Params.BrushShape)
	{
	case EDigBrushShape::Sphere:
		OutRecord.Radius = FMath::Max(XYExtent, HalfDepth);
		OutRecord.HalfExtents = FVector::ZeroVector;
		break;

	case EDigBrushShape::Cylinder:
		OutRecord.Radius = XYExtent;
		OutRecord.HalfExtents = FVector(0.0f, 0.0f, HalfDepth);
		break;

	case EDigBrushShape::Box:
		OutRecord.Radius = 0.0f;
		OutRecord.HalfExtents = FVector(XYExtent, XYExtent, HalfDepth);
		break;

	default:
		OutRecord.Radius = FMath::Max(XYExtent, HalfDepth);
		OutRecord.HalfExtents = FVector::ZeroVector;
		break;
	}

	return true;
}

// ===================================================================
// Block sampling
// ===================================================================

TArray<FRemovedBlockEntry> UDigSubsystem::SampleBlocksInVolume(const FDigParams& Params) const
{
	TArray<FRemovedBlockEntry> Entries;

	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return Entries;
	}

	const int32 R = FMath::Max(Params.Radius - 1, 0);
	const int32 Depth = FMath::Max(Params.DepthPerAction, 1);

	// ------------------------------------------------------------------
	// Important distinction:
	//
	// Surface digging:
	//   If the raw target is not a solid block, find the nearby surface and
	//   dig downward from that surface.
	//
	// Underground/cave digging:
	//   If the raw target is already a valid solid block, DO NOT snap back
	//   to the surface. Use the exact target Z so horizontal X/Y digging
	//   carves a tunnel instead of a vertical shaft.
	// ------------------------------------------------------------------

	const FGameplayTag TargetBlockType = VoxelMgr->GetBlockTypeAt(Params.TargetGridPos);
	const bool bTargetIsSolid =
		TargetBlockType.IsValid() &&
		TargetBlockType != TAG_Terrain_Bedrock;

	FIntVector AnchorBlock = Params.TargetGridPos;
	bool bUseSurfaceFollowing = !bTargetIsSolid;

	if (bUseSurfaceFollowing)
	{
		const int32 VerticalSearchRadius = FMath::Max(Depth + 8, 10);

		if (!VoxelMgr->TryFindSurfaceBlockNearZ(
			FIntPoint(Params.TargetGridPos.X, Params.TargetGridPos.Y),
			Params.TargetGridPos.Z,
			VerticalSearchRadius,
			AnchorBlock))
		{
			return Entries;
		}
	}

	for (int32 X = Params.TargetGridPos.X - R; X <= Params.TargetGridPos.X + R; ++X)
	{
		for (int32 Y = Params.TargetGridPos.Y - R; Y <= Params.TargetGridPos.Y + R; ++Y)
		{
			const int32 Dist = FMath::Max(
				FMath::Abs(X - Params.TargetGridPos.X),
				FMath::Abs(Y - Params.TargetGridPos.Y));

			if (Dist > R)
			{
				continue;
			}

			FIntVector ColumnAnchor(X, Y, AnchorBlock.Z);

			if (bUseSurfaceFollowing)
			{
				// Surface mode: each column follows its own local surface
				// so digging on slopes still behaves naturally.
				const int32 VerticalSearchRadius = FMath::Max(Depth + 8, 10);

				if (!VoxelMgr->TryFindSurfaceBlockNearZ(
					FIntPoint(X, Y),
					Params.TargetGridPos.Z,
					VerticalSearchRadius,
					ColumnAnchor))
				{
					continue;
				}
			}
			else
			{
				// Underground mode: keep the original Z plane.
				// This is what allows horizontal cave/tunnel digging.
				ColumnAnchor = FIntVector(X, Y, Params.TargetGridPos.Z);
			}

#if !UE_BUILD_SHIPPING
			if (UWorld* World = GetWorld())
			{
				const float VoxelSize = VoxelMgr->GetRenderVoxelSize();

				const FVector WorldPos = VoxelMgr->GridToWorld(
					FIntPoint(ColumnAnchor.X, ColumnAnchor.Y),
					ColumnAnchor.Z);

				DrawDebugBox(
					World,
					WorldPos,
					FVector(VoxelSize * 0.45f),
					bUseSurfaceFollowing ? FColor::Green : FColor::Orange,
					false,
					5.0f);
			}
#endif

			// Surface mode digs downward from the local surface.
			// Underground mode digs from the exact tunnel/cave target plane.
			for (int32 Z = ColumnAnchor.Z; Z > ColumnAnchor.Z - Depth; --Z)
			{
				const FIntVector Pos(X, Y, Z);

				if (!VoxelMgr->IsValidGridPosition(Pos))
				{
					continue;
				}

				const FGameplayTag BlockType = VoxelMgr->GetBlockTypeAt(Pos);

				if (BlockType.IsValid() && BlockType != TAG_Terrain_Bedrock)
				{
					FRemovedBlockEntry Entry;
					Entry.GridPosition = Pos;
					Entry.BlockType = BlockType;

					Entries.Add(MoveTemp(Entry));
				}
			}
		}
	}

	return Entries;
}

// ===================================================================
// Node reveals — Phase 2 stub
// ===================================================================

TArray<FRevealedNodeEntry> UDigSubsystem::CheckForNodeReveals(
	const TArray<FIntVector>& DugPositions,
	int32 MaxDepth)
{
	TArray<FRevealedNodeEntry> Revealed;

	UResourceSpawnSubsystem* ResourceSpawn = GetResourceSpawnSubsystem();
	if (!ResourceSpawn)
	{
		return Revealed;
	}

	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return Revealed;
	}

	const float VoxelSize = VoxelMgr->GetRenderVoxelSize();

	// Check each dug position for proximity to hidden resource nodes.
	// Detection radius = 3 voxels — close enough that the dig exposed the node.
	const float DetectionRadius = VoxelSize * 3.0f;

	TSet<int32> AlreadyRevealed;

	for (const FIntVector& GridPos : DugPositions)
	{
		const FVector WorldPos = VoxelMgr->GridToWorld(
			FIntPoint(GridPos.X, GridPos.Y), GridPos.Z);

		TArray<FSpawnedResourceNode> NearbyNodes =
			ResourceSpawn->GetNodesInRadius(WorldPos, DetectionRadius);

		for (const FSpawnedResourceNode& Node : NearbyNodes)
		{
			if (Node.bExposed || AlreadyRevealed.Contains(Node.NodeID))
			{
				continue;
			}

			// Mark the node as exposed in the spawn subsystem.
			ResourceSpawn->MarkNodeExposed(Node.NodeID);
			AlreadyRevealed.Add(Node.NodeID);

			FRevealedNodeEntry Entry;
			Entry.NodeID = Node.NodeID;
			Entry.ResourceTag = Node.ResourceTag;
			Entry.WorldPosition = Node.WorldPosition;
			Entry.Category = Node.Category;

			Revealed.Add(MoveTemp(Entry));

			UE_LOG(LogTemp, Log,
				TEXT("[DigSubsystem] Resource node REVEALED: NodeID=%d Tag=%s at (%.0f, %.0f, %.0f)"),
				Node.NodeID,
				*Node.ResourceTag.ToString(),
				Node.WorldPosition.X,
				Node.WorldPosition.Y,
				Node.WorldPosition.Z);
		}
	}

	return Revealed;
}

// ===================================================================
// Depth calculation
// ===================================================================

int32 UDigSubsystem::CalculateMaxDepthReached(const TArray<FRemovedBlockEntry>& Sampled) const
{
	if (Sampled.Num() == 0)
	{
		return 0;
	}

	int32 MaxDepth = 0;

	for (const FRemovedBlockEntry& Entry : Sampled)
	{
		const int32 Depth = GetRelativeDepth(Entry.GridPosition);
		MaxDepth = FMath::Max(MaxDepth, Depth);
	}

	return MaxDepth;
}

// ===================================================================
// EventBus dispatch
// ===================================================================

void UDigSubsystem::BroadcastDigResult(const FDigResult& Result)
{
	UEventBus* Bus = GetEventBus();
	if (!Bus)
	{
		return;
	}

	Bus->OnTerrainDug.Broadcast(Result);

	for (const FRevealedNodeEntry& Node : Result.RevealedNodes)
	{
		Bus->OnResourceNodeRevealed.Broadcast(Node);
	}
}

// ===================================================================
// Cached references
// ===================================================================

UVoxelWorldManager* UDigSubsystem::GetVoxelManager() const
{
	if (CachedVoxelManager)
	{
		return CachedVoxelManager;
	}

	UDigSubsystem* MutableThis = const_cast<UDigSubsystem*>(this);

	UWorld* World = GetWorld();

	MutableThis->CachedVoxelManager = World
		? World->GetSubsystem<UVoxelWorldManager>()
		: nullptr;

	if (!CachedVoxelManager)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] Could not resolve VoxelWorldManager."));
	}

	return CachedVoxelManager;
}

UEventBus* UDigSubsystem::GetEventBus() const
{
	if (CachedEventBus)
	{
		return CachedEventBus;
	}

	UDigSubsystem* MutableThis = const_cast<UDigSubsystem*>(this);

	UWorld* World = GetWorld();

	MutableThis->CachedEventBus = World
		? World->GetSubsystem<UEventBus>()
		: nullptr;

	if (!CachedEventBus)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigSubsystem] Could not resolve EventBus."));
	}

	return CachedEventBus;
}

UResourceSpawnSubsystem* UDigSubsystem::GetResourceSpawnSubsystem() const
{
	if (CachedResourceSpawn)
	{
		return CachedResourceSpawn;
	}

	UDigSubsystem* MutableThis = const_cast<UDigSubsystem*>(this);

	UWorld* World = GetWorld();

	MutableThis->CachedResourceSpawn = World
		? World->GetSubsystem<UResourceSpawnSubsystem>()
		: nullptr;

	if (!CachedResourceSpawn)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[DigSubsystem] ResourceSpawnSubsystem not available — node reveals disabled."));
	}

	return CachedResourceSpawn;
}
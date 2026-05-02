// DigSubsystem.cpp
// WorldForge — Phase 1: Digging Foundation

#include "Voxel/DigSubsystem.h"
#include "Voxel/VoxelWorldManager.h"
#include "Colony/EventBus.h"
#include "Data/WorldForgeTags.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

// ===================================================================
// USubsystem lifecycle
// ===================================================================

bool UDigSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Only create in game worlds (not editor preview, not dedicated server headless)
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

	UE_LOG(LogTemp, Log, TEXT("[DigSubsystem] Initialized."));
}

void UDigSubsystem::Deinitialize()
{
	CachedVoxelManager = nullptr;
	CachedEventBus = nullptr;

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

	UVoxelWorldManager* VoxelMgr = GetVoxelManager();
	if (!VoxelMgr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] DigAt failed — VoxelWorldManager not available."));
		return Result;
	}

	// 1. Gather all valid dig targets within the specified volume.
	TArray<FIntVector> Targets = GatherDigTargets(Params);

	if (Targets.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[DigSubsystem] DigAt(%s) — no valid targets in volume."),
			*Params.TargetGridPos.ToString());
		return Result;
	}

	// 2. Remove each block and record what was there.
	Result.RemovedBlocks.Reserve(Targets.Num());

	for (const FIntVector& Pos : Targets)
	{
		FRemovedBlockEntry Entry = RemoveSingleBlock(Pos);
		if (Entry.BlockType.IsValid())
		{
			Result.RemovedBlocks.Add(MoveTemp(Entry));
		}
	}

	// 3. Calculate the deepest depth reached.
	Result.DepthReached = CalculateMaxDepthReached(Result.RemovedBlocks);

	for (const FRemovedBlockEntry& Entry : Result.RemovedBlocks)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigTrace][DigSubsystem] RequestedTarget=%s RemovedGrid=%s RemovedType=%s DepthReached=%d"),
			*Params.TargetGridPos.ToString(),
			*Entry.GridPosition.ToString(),
			*Entry.BlockType.ToString(),
			Result.DepthReached);
	}

	// 4. Check for resource node reveals (Phase 2 stub — returns empty for now).
	Result.RevealedNodes = CheckForNodeReveals(Targets, Result.DepthReached);

	// 5. Dispatch via EventBus.
	if (Result.WasEffective())
	{
		BroadcastDigResult(Result);

		UE_LOG(LogTemp, Log, TEXT("[DigSubsystem] Dug %d blocks at %s, depth reached: %d, nodes revealed: %d"),
			Result.RemovedBlocks.Num(),
			*Params.TargetGridPos.ToString(),
			Result.DepthReached,
			Result.RevealedNodes.Num());
	}

	return Result;
}

bool UDigSubsystem::UndoDig(const FDigResult& Result)
{
	UVoxelWorldManager* VoxelMgr = GetVoxelManager();
	if (!VoxelMgr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] UndoDig failed — VoxelWorldManager not available."));
		return false;
	}

	bool bAllRestored = true;

	// Restore blocks in reverse order (deepest first, surface last)
	// so that any physics/visual updates cascade correctly.
	for (int32 i = Result.RemovedBlocks.Num() - 1; i >= 0; --i)
	{
		const FRemovedBlockEntry& Entry = Result.RemovedBlocks[i];

		// VoxelWorldManager::RestoreBlockAt is a Phase 1 modification to VoxelWorldManager.
		// Places a block of the given type at the grid position.
		const bool bRestored = VoxelMgr->RestoreBlockAt(Entry.GridPosition, Entry.BlockType);
		if (!bRestored)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] UndoDig — failed to restore block at %s (type: %s)"),
				*Entry.GridPosition.ToString(),
				*Entry.BlockType.ToString());
			bAllRestored = false;
		}
	}

	// TODO Phase 2+: If the dig revealed resource nodes, un-reveal them here.
	// ResourceNodeSubsystem->HideNodes(Result.RevealedNodes);

	if (bAllRestored)
	{
		UE_LOG(LogTemp, Log, TEXT("[DigSubsystem] UndoDig — restored %d blocks at %s."),
			Result.RemovedBlocks.Num(),
			*Result.DigOrigin.ToString());
	}

	return bAllRestored;
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

	FIntVector SurfaceBlock;
	if (!VoxelMgr->TryFindSurfaceBlockNearZ(
		FIntPoint(GridPos.X, GridPos.Y),
		GridPos.Z,
		18,
		SurfaceBlock))
	{
		return false;
	}

	const FGameplayTag BlockType = VoxelMgr->GetBlockTypeAt(SurfaceBlock);
	return BlockType.IsValid() && BlockType != TAG_Terrain_Bedrock;
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

	// Get the surface Z at this XY column.
	const int32 SurfaceZ = VoxelMgr->GetSurfaceZAt(FIntPoint(GridPos.X, GridPos.Y));

	// Depth = surface Z - block Z. Positive = underground.
	return SurfaceZ - GridPos.Z;
}

// ===================================================================
// Internal helpers
// ===================================================================

TArray<FIntVector> UDigSubsystem::GatherDigTargets(const FDigParams& Params) const
{
	TArray<FIntVector> Targets;

	const UVoxelWorldManager* VoxelMgr = const_cast<UDigSubsystem*>(this)->GetVoxelManager();
	if (!VoxelMgr)
	{
		return Targets;
	}

	const int32 R = FMath::Max(Params.Radius - 1, 0);
	const int32 Depth = FMath::Max(Params.DepthPerAction, 1);

	// Lets the brush climb/descend across nearby slope columns.
	// Depth + 2 gives a little forgiveness without jumping to a mountain crest.
	const int32 VerticalSearchRadius = FMath::Max(Depth + 8, 10);

	for (int32 X = Params.TargetGridPos.X - R; X <= Params.TargetGridPos.X + R; ++X)
	{
		for (int32 Y = Params.TargetGridPos.Y - R; Y <= Params.TargetGridPos.Y + R; ++Y)
		{
			const int32 Dist = FMath::Max(
				FMath::Abs(X - Params.TargetGridPos.X),
				FMath::Abs(Y - Params.TargetGridPos.Y)
			);

			if (Dist > R)
			{
				continue;
			}

			FIntVector LocalSurface;
			if (!VoxelMgr->TryFindSurfaceBlockNearZ(
				FIntPoint(X, Y),
				Params.TargetGridPos.Z,
				VerticalSearchRadius,
				LocalSurface))
			{
				continue;
			}

			if (UWorld* World = GetWorld())
			{
				const float VoxelSize = VoxelMgr->GetRenderVoxelSize();
				const FVector WorldPos = VoxelMgr->GridToWorld(
					FIntPoint(LocalSurface.X, LocalSurface.Y),
					LocalSurface.Z
				);

				DrawDebugBox(
					World,
					WorldPos,
					FVector(VoxelSize * 0.45f),
					FColor::Green,
					false,
					5.0f
				);
			}

			for (int32 Z = LocalSurface.Z; Z > LocalSurface.Z - Depth; --Z)
			{
				const FIntVector Pos(X, Y, Z);
				const FGameplayTag BlockType = VoxelMgr->GetBlockTypeAt(Pos);

				if (BlockType.IsValid() && BlockType != TAG_Terrain_Bedrock)
				{
					Targets.Add(Pos);
				}
			}
		}
	}

	return Targets;
}

FRemovedBlockEntry UDigSubsystem::RemoveSingleBlock(const FIntVector& GridPos)
{
	FRemovedBlockEntry Entry;
	Entry.GridPosition = GridPos;

	UVoxelWorldManager* VoxelMgr = GetVoxelManager();
	if (!VoxelMgr)
	{
		return Entry;
	}

	// Read the block type before removing it (needed for undo).
	Entry.BlockType = VoxelMgr->GetBlockTypeAt(GridPos);

	if (!Entry.BlockType.IsValid())
	{
		// Already air — nothing to remove.
		return Entry;
	}

	// Actually remove the block.
	const bool bRemoved = VoxelMgr->RemoveBlockAt(GridPos);
	if (!bRemoved)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] RemoveSingleBlock — VoxelWorldManager refused removal at %s"),
			*GridPos.ToString());
		Entry.BlockType = FGameplayTag(); // Invalidate since removal failed.
	}

	return Entry;
}

TArray<FRevealedNodeEntry> UDigSubsystem::CheckForNodeReveals(
	const TArray<FIntVector>& DugPositions, int32 MaxDepth)
{
	TArray<FRevealedNodeEntry> Revealed;

	// ---------------------------------------------------------------
	// PHASE 2 STUB
	// ---------------------------------------------------------------
	// When ResourceNodeSubsystem exists (Phase 2), this will:
	//
	// UResourceNodeSubsystem* NodeSub = GetWorld()->GetSubsystem<UResourceNodeSubsystem>();
	// if (NodeSub)
	// {
	//     for (const FIntVector& Pos : DugPositions)
	//     {
	//         TArray<FRevealedNodeEntry> NodesAtPos = NodeSub->TryRevealNodesAt(Pos);
	//         Revealed.Append(NodesAtPos);
	//     }
	// }
	//
	// For each revealed node, dispatch OnResourceNodeRevealed via EventBus.
	// ---------------------------------------------------------------

	return Revealed;
}

int32 UDigSubsystem::CalculateMaxDepthReached(const TArray<FRemovedBlockEntry>& Removed) const
{
	if (Removed.Num() == 0)
	{
		return 0;
	}

	int32 MaxDepth = 0;

	for (const FRemovedBlockEntry& Entry : Removed)
	{
		const int32 Depth = GetRelativeDepth(Entry.GridPosition);
		MaxDepth = FMath::Max(MaxDepth, Depth);
	}

	return MaxDepth;
}

void UDigSubsystem::BroadcastDigResult(const FDigResult& Result)
{
	UEventBus* Bus = GetEventBus();
	if (!Bus)
	{
		return;
	}

	// EventBus::Dispatch expects a gameplay tag identifying the event
	// and a void* payload (or your project's typed dispatch pattern).
	// Adjust this call to match your actual EventBus::Dispatch signature.
	//
	// Expected EventBus modification (Phase 1):
	//   Add FOnTerrainDug delegate: DECLARE_MULTICAST_DELEGATE_OneParam(FOnTerrainDug, const FDigResult&);
	//   Bus->OnTerrainDug.Broadcast(Result);

	Bus->OnTerrainDug.Broadcast(Result);

	// If any nodes were revealed, also broadcast per-node events.
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

	// VoxelWorldManager may be a UWorldSubsystem, UGameInstanceSubsystem,
	// or an actor component depending on your architecture.
	// Adjust this resolution to match your actual setup.
	//
	// If it's a UWorldSubsystem:
	UDigSubsystem* MutableThis = const_cast<UDigSubsystem*>(this);
	
	UWorld* World = GetWorld();
	MutableThis->CachedVoxelManager = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;

	if (!CachedVoxelManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] Could not resolve VoxelWorldManager."));
	}

	return CachedVoxelManager;
}

UEventBus* UDigSubsystem::GetEventBus() const
{
	if (CachedEventBus)
	{
		return CachedEventBus;
	}

	// Adjust resolution to match your EventBus location.
	// If it's a UWorldSubsystem:
	UDigSubsystem* MutableThis = const_cast<UDigSubsystem*>(this);
	
	UWorld* World = GetWorld();
	MutableThis->CachedEventBus = World ? World->GetSubsystem<UEventBus>() : nullptr;

	if (!CachedEventBus)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DigSubsystem] Could not resolve EventBus."));
	}

	return CachedEventBus;
}

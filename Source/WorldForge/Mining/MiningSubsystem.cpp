#include "Mining/MiningSubsystem.h"

#include "Voxel/VoxelWorldManager.h"
#include "World/WorldMapSubsystem.h"
#include "Colony/SettlementManager.h"
#include "Colony/ProcessorManager.h"
#include "Colony/Processors/JobProcessor.h"
#include "Data/WorldForgeTags.h"
#include "Mining/StabilitySubsystem.h"
#include "Engine/World.h"
#include "Grid/GridSelectionSubsystem.h"   // Patch 2E: confirmed overlay removal
#include "Grid/GridDesignationTypes.h"      // Patch 2E: EGridDesignationType::Mine

// ===================================================================
// Lifecycle
// ===================================================================

void UMiningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	VoxelWorldManager = nullptr;
	UE_LOG(LogTemp, Log, TEXT("[MiningSubsystem] Initialized using public VoxelWorldManager mining API."));
}

void UMiningSubsystem::Deinitialize()
{
	Designations.Reset();
	VoxelWorldManager = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[MiningSubsystem] Deinitialized."));
	Super::Deinitialize();
}

// ===================================================================
// Internal helpers
// ===================================================================

bool UMiningSubsystem::IsDesignationTerminal(const FMiningDesignation& Designation) const
{
	return
		Designation.State == EMiningDesignationState::Cancelled ||
		Designation.State == EMiningDesignationState::Complete ||
		Designation.State == EMiningDesignationState::Failed;
}

UVoxelWorldManager* UMiningSubsystem::GetVoxelWorldManager()
{
	if (!VoxelWorldManager)
	{
		if (UWorld* World = GetWorld())
		{
			VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
		}
	}
	return VoxelWorldManager;
}

bool UMiningSubsystem::ResolveMineTarget(
	FIntVector GridPosition,
	FIntVector& OutResolvedGridPosition,
	FGameplayTag& OutBlockType)
{
	OutResolvedGridPosition = GridPosition;
	OutBlockType = FGameplayTag();

	UVoxelWorldManager* VM = GetVoxelWorldManager();
	if (!VM)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] ResolveMineTarget failed. VoxelWorldManager unavailable."));
		return false;
	}

	const FGameplayTag ExactType = VM->GetBlockTypeAt(GridPosition);
	if (ExactType.IsValid())
	{
		OutResolvedGridPosition = GridPosition;
		OutBlockType = ExactType;

		UE_LOG(LogTemp, Log,
			TEXT("[CUBE-MINING-01] MineTargetCheck Voxel=(%d,%d,%d) Tag=%s Mineable=%s Reason=%s"),
			OutResolvedGridPosition.X,
			OutResolvedGridPosition.Y,
			OutResolvedGridPosition.Z,
			*OutBlockType.ToString(),
			TEXT("true"),
			TEXT("ValidSolidTag"));

		return true;
	}

	// Surface raycasts can land slightly above the actual solid voxel.
	for (int32 OffsetZ = 1; OffsetZ <= 12; ++OffsetZ)
	{
		const FIntVector ProbePos(GridPosition.X, GridPosition.Y, GridPosition.Z - OffsetZ);
		const FGameplayTag ProbeType = VM->GetBlockTypeAt(ProbePos);

		if (ProbeType.IsValid())
		{
			OutResolvedGridPosition = ProbePos;
			OutBlockType = ProbeType;

			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] Adjusted mine target DOWN from %s to %s Type=%s"),
				*GridPosition.ToString(),
				*OutResolvedGridPosition.ToString(),
				*OutBlockType.ToString());

			UE_LOG(LogTemp, Log,
				TEXT("[CUBE-MINING-01] MineTargetCheck Voxel=(%d,%d,%d) Tag=%s Mineable=%s Reason=%s"),
				OutResolvedGridPosition.X,
				OutResolvedGridPosition.Y,
				OutResolvedGridPosition.Z,
				*OutBlockType.ToString(),
				TEXT("true"),
				TEXT("ValidSolidTag"));

			return true;
		}
	}

	for (int32 OffsetZ = 1; OffsetZ <= 4; ++OffsetZ)
	{
		const FIntVector ProbePos(GridPosition.X, GridPosition.Y, GridPosition.Z + OffsetZ);
		const FGameplayTag ProbeType = VM->GetBlockTypeAt(ProbePos);

		if (ProbeType.IsValid())
		{
			OutResolvedGridPosition = ProbePos;
			OutBlockType = ProbeType;

			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] Adjusted mine target UP from %s to %s Type=%s"),
				*GridPosition.ToString(),
				*OutResolvedGridPosition.ToString(),
				*OutBlockType.ToString());

			UE_LOG(LogTemp, Log,
				TEXT("[CUBE-MINING-01] MineTargetCheck Voxel=(%d,%d,%d) Tag=%s Mineable=%s Reason=%s"),
				OutResolvedGridPosition.X,
				OutResolvedGridPosition.Y,
				OutResolvedGridPosition.Z,
				*OutBlockType.ToString(),
				TEXT("true"),
				TEXT("ValidSolidTag"));

			return true;
		}
	}

	// [CUBE-MINING-V3-ACCESS] Underground per-voxel fallback.
	// GetBlockTypeAt routes straight to CubeTerrainRuntime, whose grid is 0-based and
	// non-negative. Underground mine selections are per-voxel coords that are commonly
	// NEGATIVE (e.g. (46,-111,17)), so the raw runtime lookup returns an invalid tag and
	// the ExactType/Z-probe checks above all fail — even though the cell is perfectly
	// valid solid ground. This is the same coordinate-space mismatch already fixed in
	// GridSelectionSubsystem's access filter.
	//
	// Authoritative validity test: a cell is solid (mineable) if it sits AT or BELOW the
	// surface height of its column. Resolve the column surface via the same proven chain
	// the access filter uses: per-voxel cell -> world tile (VoxelCellToChunkCell, the
	// project's blessed conversion) -> preferred branch of TryGetTerrainSurfaceVoxelZ.
	{
		// CUBE-VOXEL-SCALE FIX: resolve the column surface through the SAME chain the
		// confirm side uses (GridToWorld -> WorldPositionToTileXY -> TryGetTerrainSurfaceVoxelZ).
		// VoxelCellToChunkCell's integer recenter math broke after the 200uu scale change:
		// it divided by GetChunkSizeInVoxels (32->16) with a tile-unit HalfGrid recenter,
		// producing a DIFFERENT, often negative tile (e.g. (-122,101)) than the confirm
		// path's (134,357) for the same voxel — so the surface lookup hit the wrong column
		// (Z=18 instead of 24) and every cell was wrongly RejectAboveSurface. Going through
		// world space makes this path scale-independent and consistent with selection.
		FIntPoint WorldTileXY(GridPosition.X, GridPosition.Y);
		int32 ColumnSurfaceZ = 0;
		bool bHasColumnSurface = false;

		const UWorld* ResolveWorld = GetWorld();
		const UWorldMapSubsystem* WorldMap_Resolve =
			ResolveWorld ? ResolveWorld->GetSubsystem<UWorldMapSubsystem>() : nullptr;

		if (WorldMap_Resolve)
		{
			const FVector CellWorld = VM->GridToWorld(FIntPoint(GridPosition.X, GridPosition.Y), GridPosition.Z);
			int32 TileX = INDEX_NONE;
			int32 TileY = INDEX_NONE;
			if (WorldMap_Resolve->WorldPositionToTileXY(CellWorld, TileX, TileY))
			{
				WorldTileXY = FIntPoint(TileX, TileY);
				bHasColumnSurface = VM->TryGetTerrainSurfaceVoxelZ(WorldTileXY, ColumnSurfaceZ);
			}
		}

		// Defensive fallback to the per-voxel overload only if the world-tile chain failed.
		if (!bHasColumnSurface)
		{
			bHasColumnSurface =
				VM->TryGetTerrainSurfaceVoxelZ(FIntPoint(GridPosition.X, GridPosition.Y), ColumnSurfaceZ);
		}

		if (bHasColumnSurface && GridPosition.Z <= ColumnSurfaceZ)
		{
			// Solid ground at/below the column surface: a valid mine target. We don't have
			// a precise per-voxel block tag here (the runtime can't tag this cell at these
			// coords), so report generic stone — OutBlockType is used only for logging and
			// resolved-position bookkeeping downstream, not for gating.
			OutResolvedGridPosition = GridPosition;
			OutBlockType = TAG_Terrain_Stone;

			UE_LOG(LogTemp, Log,
				TEXT("[CUBE-MINING-01] MineTargetCheck Voxel=(%d,%d,%d) Tag=%s Mineable=%s Reason=%s"),
				GridPosition.X, GridPosition.Y, GridPosition.Z,
				*OutBlockType.ToString(),
				TEXT("true"),
				TEXT("ColumnSurfaceSolid"));

			UE_LOG(LogTemp, Log,
				TEXT("[CUBE-MINING-V3-ACCESS] UndergroundResolve Voxel=(%d,%d,%d) WorldTile=(%d,%d) ColumnSurfaceZ=%d Result=ValidSolid"),
				GridPosition.X, GridPosition.Y, GridPosition.Z,
				WorldTileXY.X, WorldTileXY.Y, ColumnSurfaceZ);

			return true;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[CUBE-MINING-V3-ACCESS] UndergroundResolve Voxel=(%d,%d,%d) WorldTile=(%d,%d) HasColumnSurface=%s ColumnSurfaceZ=%d Result=RejectAboveSurface"),
			GridPosition.X, GridPosition.Y, GridPosition.Z,
			WorldTileXY.X, WorldTileXY.Y,
			bHasColumnSurface ? TEXT("true") : TEXT("false"),
			ColumnSurfaceZ);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[CUBE-MINING-01] MineTargetCheck Voxel=(%d,%d,%d) Tag=%s Mineable=%s Reason=%s"),
		GridPosition.X,
		GridPosition.Y,
		GridPosition.Z,
		TEXT("<Invalid>"),
		TEXT("false"),
		TEXT("NoValidTag"));

	UE_LOG(LogTemp, Warning,
		TEXT("[MiningSubsystem] Invalid mine target after probe. Grid=%s"),
		*GridPosition.ToString());

	return false;
}

bool UMiningSubsystem::IsValidMineTarget(FIntVector GridPosition, FGameplayTag& OutBlockType)
{
	FIntVector ResolvedGridPosition;
	return ResolveMineTarget(GridPosition, ResolvedGridPosition, OutBlockType);
}

// ===================================================================
// Designation creation
// ===================================================================

bool UMiningSubsystem::CreateMiningDesignation(
	int32 SettlementID,
	FIntVector GridPosition,
	int32 CurrentStep,
	int32& OutDesignationID)
{
	EMineDesignationResult Result = EMineDesignationResult::CreateFailed;
	return CreateMiningDesignationEx(SettlementID, GridPosition, CurrentStep, OutDesignationID, Result);
}

bool UMiningSubsystem::CreateMiningDesignationEx(
	int32 SettlementID,
	FIntVector GridPosition,
	int32 CurrentStep,
	int32& OutDesignationID,
	EMineDesignationResult& OutResult)
{
	OutDesignationID = INDEX_NONE;
	OutResult = EMineDesignationResult::CreateFailed;

	FGameplayTag BlockType;
	FIntVector ResolvedGridPosition;

	if (!ResolveMineTarget(GridPosition, ResolvedGridPosition, BlockType))
	{
		OutResult = EMineDesignationResult::InvalidTarget;
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CreateMiningDesignation failed. Invalid target %s"),
			*GridPosition.ToString());
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		if (const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>())
		{
			const int32 PlayerSettlementID = SettlementManager->GetPlayerSettlementID();
			const bool bShouldValidatePlayerClaim =
				PlayerSettlementID != INDEX_NONE &&
				SettlementID == PlayerSettlementID;

			int32 WorldTileID = INDEX_NONE;
			FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
			int32 ClaimedWorldTileCount = 0;
			bool bClaimTileResolved = false;
			bool bClaimedByPlayerSettlement = false;

			const FIntPoint ClaimCheckVoxelCell(ResolvedGridPosition.X, ResolvedGridPosition.Y);

			if (const TArray<int32>* ClaimedTiles = SettlementManager->GetClaimedWorldTilesForSettlement(PlayerSettlementID))
			{
				ClaimedWorldTileCount = ClaimedTiles->Num();
			}

			if (bShouldValidatePlayerClaim)
			{
				UVoxelWorldManager* VM = GetVoxelWorldManager();
				const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>();

				// CubePrototype mine selections are voxel/grid coordinates.
				// Settlement claims are stored as world-map tile IDs.
				// Convert voxel XY -> world tile XY before checking ownership.
				//
				// Resolve the world tile through the SAME proven chain the confirm path
				// already uses (GridToWorld -> WorldPositionToTileXY), which correctly
				// produced TerrainTile=(97,107) for this exact cell. VoxelCellToChunkCell
				// must NOT be used here: it recenters using CachedTerrainGridWidth*0.5,
				// which is 0/unset at this point, so it produced a bogus NEGATIVE tile
				// (e.g. (-31,-21)) that TileXYToIndex could not resolve -> the claim check
				// failed and every underground designation was blocked as "unclaimed land"
				// even on owned columns.
				if (VM && WorldMapSubsystem)
				{
					const FVector ClaimWorld =
						VM->GridToWorld(ClaimCheckVoxelCell, ResolvedGridPosition.Z);

					int32 ClaimTileX = INDEX_NONE;
					int32 ClaimTileY = INDEX_NONE;
					if (WorldMapSubsystem->WorldPositionToTileXY(ClaimWorld, ClaimTileX, ClaimTileY))
					{
						WorldTileXY = FIntPoint(ClaimTileX, ClaimTileY);
						bClaimTileResolved = WorldMapSubsystem->TileXYToIndex(WorldTileXY.X, WorldTileXY.Y, WorldTileID);
					}
				}

				bClaimedByPlayerSettlement =
					bClaimTileResolved &&
					SettlementManager->DoesSettlementClaimWorldTile(PlayerSettlementID, WorldTileID);
			}

			// CUBE-MINING-V3-CLAIM: entry-vs-deeper claim rule.
			// Design: the mine ENTRANCE must start on owned land, but a connected tunnel
			// may run UNDER unclaimed/rival territory. So ownership is required only for
			// the entry cell — the one at its column surface (the shaft mouth / first
			// mineable layer). Deeper cells (Z strictly below the column surface) skip the
			// ownership requirement. Column surface is resolved via the same chain
			// ResolveMineTarget used. If it can't be resolved, fail safe: treat as entry
			// (claim still required) so we never accidentally allow mining anywhere.
			bool bIsEntryCell = true;
			if (UVoxelWorldManager* VM_Surface = GetVoxelWorldManager())
			{
				// CUBE-VOXEL-SCALE FIX: same world-tile chain as ResolveMineTarget, not
				// VoxelCellToChunkCell (which broke after the 200uu scale change).
				const UWorldMapSubsystem* WorldMap_Entry = World->GetSubsystem<UWorldMapSubsystem>();
				int32 ColumnSurfaceZ = 0;
				bool bHasSurface = false;

				if (WorldMap_Entry)
				{
					const FVector EntryWorld =
						VM_Surface->GridToWorld(FIntPoint(ResolvedGridPosition.X, ResolvedGridPosition.Y), ResolvedGridPosition.Z);
					int32 ETileX = INDEX_NONE;
					int32 ETileY = INDEX_NONE;
					if (WorldMap_Entry->WorldPositionToTileXY(EntryWorld, ETileX, ETileY))
					{
						bHasSurface = VM_Surface->TryGetTerrainSurfaceVoxelZ(FIntPoint(ETileX, ETileY), ColumnSurfaceZ);
					}
				}
				if (!bHasSurface)
				{
					bHasSurface = VM_Surface->TryGetTerrainSurfaceVoxelZ(
						FIntPoint(ResolvedGridPosition.X, ResolvedGridPosition.Y), ColumnSurfaceZ);
				}
				if (bHasSurface)
				{
					bIsEntryCell = (ResolvedGridPosition.Z >= ColumnSurfaceZ);
				}
			}

			const bool bClaimRequiredForThisCell = bShouldValidatePlayerClaim && bIsEntryCell;

			UE_LOG(LogTemp, Log,
				TEXT("[CUBE-MINING-V2] MineClaimWorldTile InputVoxelCell=(%d,%d) ResolvedVoxel=(%d,%d,%d) ClaimTile=(%d,%d) WorldTileID=%d Resolved=%s PlayerSettlementID=%d ClaimedTileCount=%d Claimed=%s"),
				ClaimCheckVoxelCell.X,
				ClaimCheckVoxelCell.Y,
				ResolvedGridPosition.X,
				ResolvedGridPosition.Y,
				ResolvedGridPosition.Z,
				WorldTileXY.X,
				WorldTileXY.Y,
				WorldTileID,
				bClaimTileResolved ? TEXT("true") : TEXT("false"),
				PlayerSettlementID,
				ClaimedWorldTileCount,
				bClaimedByPlayerSettlement ? TEXT("true") : TEXT("false"));

			if (bClaimRequiredForThisCell && !bClaimedByPlayerSettlement)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MiningSubsystem] CreateMiningDesignation blocked: unclaimed land (entry cell). Settlement=%d Voxel=(%d,%d,%d) ClaimTile=(%d,%d) WorldTileID=%d TileResolved=%s ClaimedTileCount=%d"),
					SettlementID,
					ResolvedGridPosition.X,
					ResolvedGridPosition.Y,
					ResolvedGridPosition.Z,
					WorldTileXY.X,
					WorldTileXY.Y,
					WorldTileID,
					bClaimTileResolved ? TEXT("true") : TEXT("false"),
					ClaimedWorldTileCount);
				OutResult = EMineDesignationResult::Unclaimed;
				return false;
			}
			else if (bShouldValidatePlayerClaim && !bIsEntryCell && !bClaimedByPlayerSettlement)
			{
				// Deeper cell under unclaimed land — allowed by the entry-vs-deeper rule.
				UE_LOG(LogTemp, Log,
					TEXT("[CUBE-MINING-V3-CLAIM] Deep cell allowed under unclaimed land. Voxel=(%d,%d,%d) ClaimTile=(%d,%d)"),
					ResolvedGridPosition.X, ResolvedGridPosition.Y, ResolvedGridPosition.Z,
					WorldTileXY.X, WorldTileXY.Y);
			}
		}
	}


	for (const TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		const FMiningDesignation& Existing = Pair.Value;

		if (Existing.GridPosition == ResolvedGridPosition && !IsDesignationTerminal(Existing))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] Duplicate mining designation ignored at %s ExistingID=%d State=%d"),
				*ResolvedGridPosition.ToString(),
				Existing.DesignationID,
				static_cast<int32>(Existing.State));

			OutResult = EMineDesignationResult::Duplicate;
			return false;
		}
	}

	FMiningDesignation NewDesignation;
	NewDesignation.DesignationID = NextDesignationID++;
	NewDesignation.SettlementID = SettlementID;
	NewDesignation.GridPosition = ResolvedGridPosition;
	NewDesignation.OriginalGridPosition = GridPosition;  // Patch 2E: preserve overlay commit coord
	NewDesignation.ExpectedBlockType = BlockType;
	NewDesignation.State = EMiningDesignationState::Pending;
	NewDesignation.ReservedAgentID = INDEX_NONE;
	NewDesignation.CreatedStep = CurrentStep;
	NewDesignation.BrushShape = EDigBrushShape::Box;
	NewDesignation.RemovalMode = CurrentRemovalMode;
	NewDesignation.AppliedBrushID = INDEX_NONE;

	Designations.Add(NewDesignation.DesignationID, NewDesignation);
	OutDesignationID = NewDesignation.DesignationID;

	UE_LOG(LogTemp, Warning,
		TEXT("[MiningSubsystem] Created designation ID=%d Settlement=%d Grid=%s Type=%s Step=%d BrushShape=%d RemovalMode=%d"),
		OutDesignationID,
		SettlementID,
		*ResolvedGridPosition.ToString(),
		*BlockType.ToString(),
		CurrentStep,
		static_cast<int32>(NewDesignation.BrushShape),
		static_cast<int32>(NewDesignation.RemovalMode));

	OutResult = EMineDesignationResult::Created;
	return true;
}

// ===================================================================
// Designation cancellation
// ===================================================================

bool UMiningSubsystem::CancelMiningDesignation(int32 DesignationID)
{
	FMiningDesignation* Designation = Designations.Find(DesignationID);
	if (!Designation)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] Cancel failed. Missing designation ID=%d"),
			DesignationID);
		return false;
	}

	if (Designation->State == EMiningDesignationState::Complete)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] Cancel failed. Designation already complete ID=%d"),
			DesignationID);
		return false;
	}

	Designation->State = EMiningDesignationState::Cancelled;
	Designation->ReservedAgentID = INDEX_NONE;

	UE_LOG(LogTemp, Log,
		TEXT("[MiningSubsystem] Cancelled designation ID=%d"),
		DesignationID);

	return true;
}

// ===================================================================
// Designation reservation
// ===================================================================

bool UMiningSubsystem::TryReserveDesignation(
	int32 SettlementID,
	int32 AgentID,
	int32& OutDesignationID,
	FIntVector& OutGridPosition)
{
	OutDesignationID = INDEX_NONE;
	OutGridPosition = FIntVector::ZeroValue;

	// Cache subsystem pointers once outside the loop.
	UVoxelWorldManager* VM = GetVoxelWorldManager();
	UStabilitySubsystem* StabSys = GetWorld() ? GetWorld()->GetSubsystem<UStabilitySubsystem>() : nullptr;

	for (TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		FMiningDesignation& Designation = Pair.Value;

		if (Designation.SettlementID != SettlementID) { continue; }
		if (Designation.State != EMiningDesignationState::Pending) { continue; }

		FGameplayTag CurrentBlockType;
		FIntVector   ResolvedGridPosition;

		if (!ResolveMineTarget(Designation.GridPosition, ResolvedGridPosition, CurrentBlockType))
		{
			Designation.State = EMiningDesignationState::Failed;
			Designation.ReservedAgentID = INDEX_NONE;

			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] Reservation failed validation. ID=%d Grid=%s"),
				Designation.DesignationID,
				*Designation.GridPosition.ToString());

			continue;
		}

		// ── Underground access check ────────────────────────────────────────
		// Do not let agents claim designations they physically cannot reach yet.
		// A voxel is accessible if:
		//   (a) It is at or near the terrain surface (≤ 1 layer below surface Z), OR
		//   (b) There is at least one already-mined (air) voxel in the column
		//       between this voxel and the surface, giving colonists a path in.
		//
		// Inaccessible designations remain Pending and will be picked up
		// automatically once higher layers are dug out.
		if (StabSys && VM)
		{
			const int32 SurfaceZ = VM->GetSurfaceZAt(FIntPoint(ResolvedGridPosition.X, ResolvedGridPosition.Y));
			const int32 DepthBelowSurface = SurfaceZ - ResolvedGridPosition.Z;

			if (DepthBelowSurface > 1)
			{
				// Deep underground — require an air column above.
				bool bAccessible = false;

				for (int32 CheckZ = ResolvedGridPosition.Z + 1; CheckZ <= SurfaceZ; ++CheckZ)
				{
					if (StabSys->IsAirVoxel(FIntVector(ResolvedGridPosition.X, ResolvedGridPosition.Y, CheckZ)))
					{
						bAccessible = true;
						break;
					}
				}

				if (!bAccessible)
				{
					UE_LOG(LogTemp, Verbose,
						TEXT("[MiningSubsystem] Designation ID=%d at (%d,%d,%d) depth=%d not yet accessible — no air path above. Skipping."),
						Designation.DesignationID,
						ResolvedGridPosition.X, ResolvedGridPosition.Y, ResolvedGridPosition.Z,
						DepthBelowSurface);

					continue;  // Leave Pending; try again next step after above layers are mined.
				}
			}
		}
		// ── End access check ────────────────────────────────────────────────

		Designation.GridPosition = ResolvedGridPosition;
		Designation.ExpectedBlockType = CurrentBlockType;
		Designation.State = EMiningDesignationState::Reserved;
		Designation.ReservedAgentID = AgentID;

		OutDesignationID = Designation.DesignationID;
		OutGridPosition = Designation.GridPosition;

		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] Reserved designation ID=%d Agent=%d Grid=%s Type=%s"),
			OutDesignationID,
			AgentID,
			*OutGridPosition.ToString(),
			*CurrentBlockType.ToString());

		return true;
	}

	return false;
}

// ===================================================================
// Job posting
// ===================================================================

void UMiningSubsystem::PostMiningJobs(int32 SettlementID, int32 StepNumber)
{
	UWorld* World = GetWorld();
	UProcessorManager* ProcessorManager = World ? World->GetSubsystem<UProcessorManager>() : nullptr;
	UJobProcessor* JobProcessor = ProcessorManager ? ProcessorManager->GetTypedProcessor<UJobProcessor>() : nullptr;

	if (!JobProcessor)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[MiningSubsystem] PostMiningJobs skipped: JobProcessor=null Settlement=%d Step=%d"),
			SettlementID,
			StepNumber);

		return;
	}

	auto HasExistingMiningJob = [JobProcessor](int32 InSettlementID, int32 DesignationID) -> bool
		{
			for (const FJobRequest& ExistingJob : JobProcessor->GetAllJobs())
			{
				if (ExistingJob.SettlementID == InSettlementID &&
					ExistingJob.JobTag == TAG_Job_Mine &&
					ExistingJob.TargetID == DesignationID)
				{
					return true;
				}
			}

			return false;
		};

	int32 PostedCount = 0;

	for (TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		FMiningDesignation& Designation = Pair.Value;

		if (Designation.SettlementID != SettlementID) { continue; }
		if (Designation.State != EMiningDesignationState::Pending) { continue; }
		if (HasExistingMiningJob(SettlementID, Designation.DesignationID)) { continue; }

		FGameplayTag CurrentBlockType;
		FIntVector   ResolvedGridPosition;

		if (!ResolveMineTarget(Designation.GridPosition, ResolvedGridPosition, CurrentBlockType))
		{
			Designation.State = EMiningDesignationState::Failed;
			Designation.ReservedAgentID = INDEX_NONE;

			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] PostMiningJobs failed validation. ID=%d Grid=%s"),
				Designation.DesignationID,
				*Designation.GridPosition.ToString());

			continue;
		}

		Designation.GridPosition = ResolvedGridPosition;
		Designation.ExpectedBlockType = CurrentBlockType;

		FJobRequest Req;
		Req.SettlementID = SettlementID;
		Req.JobTag = TAG_Job_Mine;
		Req.RequiredSkill = TAG_Skill_Mining;
		Req.Priority = EJobPriority::High;
		Req.PostedOnStep = StepNumber;

		// Critical metadata:
		// TargetID = Mining DesignationID
		// TargetGridPos = X/Y nav target for the miner
		Req.TargetID = Designation.DesignationID;
		Req.TargetGridPos = FIntPoint(Designation.GridPosition.X, Designation.GridPosition.Y);
		Req.TargetItemTag = TAG_Item_Stone;

		JobProcessor->PostJob(Req);
		++PostedCount;

		UE_LOG(LogTemp, Log,
			TEXT("[MiningSubsystem] Posted mining job DesignationID=%d Settlement=%d Grid=%s Step=%d"),
			Designation.DesignationID,
			SettlementID,
			*Designation.GridPosition.ToString(),
			StepNumber);
	}

	if (PostedCount > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MiningSubsystem] Posted %d mining job(s) for Settlement=%d Step=%d"),
			PostedCount,
			SettlementID,
			StepNumber);
	}
}

// ===================================================================
// Designation completion
// ===================================================================

bool UMiningSubsystem::CompleteDesignation(int32 DesignationID, int32 AgentID)
{
	FMiningDesignation* Designation = Designations.Find(DesignationID);

	UVoxelWorldManager* VM = GetVoxelWorldManager();
	if (!Designation)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed. Missing designation ID=%d Agent=%d"),
			DesignationID,
			AgentID);
		return false;
	}

	if (!VM)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed. VoxelWorldManager unavailable. ID=%d Agent=%d"),
			DesignationID,
			AgentID);
		return false;
	}

	if (Designation->State != EMiningDesignationState::Reserved)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed. ID=%d wrong state=%d Agent=%d"),
			DesignationID,
			static_cast<int32>(Designation->State),
			AgentID);
		return false;
	}

	if (Designation->ReservedAgentID != AgentID)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed. ID=%d reserved by Agent=%d but completed by Agent=%d"),
			DesignationID,
			Designation->ReservedAgentID,
			AgentID);
		return false;
	}

	// Revalidate the designation at completion time.
	FGameplayTag CurrentBlockType;
	FIntVector   ResolvedGridPosition;

	if (!ResolveMineTarget(Designation->GridPosition, ResolvedGridPosition, CurrentBlockType))
	{
		Designation->State = EMiningDesignationState::Failed;
		Designation->ReservedAgentID = INDEX_NONE;

		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed validation. ID=%d Grid=%s"),
			DesignationID,
			*Designation->GridPosition.ToString());

		return false;
	}

	Designation->GridPosition = ResolvedGridPosition;
	Designation->ExpectedBlockType = CurrentBlockType;

	UE_LOG(LogTemp, Warning,
		TEXT("[MiningSubsystem] CompleteDesignation removing block. ID=%d Agent=%d Grid=%s ExpectedType=%s Shape=%d RemovalMode=%d"),
		DesignationID,
		AgentID,
		*Designation->GridPosition.ToString(),
		*Designation->ExpectedBlockType.ToString(),
		static_cast<int32>(Designation->BrushShape),
		static_cast<int32>(Designation->RemovalMode));

	const bool bRemoved = VM->RemoveVoxelByMode(Designation->GridPosition, Designation->RemovalMode);

	if (!bRemoved)
	{
		Designation->State = EMiningDesignationState::Failed;
		Designation->ReservedAgentID = INDEX_NONE;

		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed. RemoveVoxelByMode failed. ID=%d Agent=%d Grid=%s Type=%s"),
			DesignationID,
			AgentID,
			*Designation->GridPosition.ToString(),
			*Designation->ExpectedBlockType.ToString());

		return false;
	}

	// ── Structural stability check ─────────────────────────────────────────
	// Now that the voxel is physically removed, ask the stability system whether
	// any ceiling tiles have lost support and need to cave in.
	//
	// ProcessCollapses inside OnVoxelRemoved calls DigSubsystem::DigVoxelAtGrid
	// for each cascading voxel, so no further Voxel Plugin calls are needed here.
	if (UStabilitySubsystem* StabSys = GetWorld() ? GetWorld()->GetSubsystem<UStabilitySubsystem>() : nullptr)
	{
		FCollapseResult Collapse = StabSys->OnVoxelRemoved(
			Designation->GridPosition,
			Designation->SettlementID);

		if (Collapse.WasTriggered())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] Cave-in triggered by DesignationID=%d. Collapsed=%d Injured=%d CascadeDepth=%d"),
				DesignationID,
				Collapse.CollapsedVoxels.Num(),
				Collapse.InjuredAgentIDs.Num(),
				Collapse.MaxCascadeDepthReached);

			// Cancel any standing designations that now point into collapsed air.
			for (const FIntVector& CollapsedVoxel : Collapse.CollapsedVoxels)
			{
				CancelDesignationsAtVoxel(CollapsedVoxel, Designation->SettlementID);
			}

			// TODO: Forward Collapse.InjuredAgentIDs to EventBus so HealthProcessor
			// can apply cave-in damage (TAG_Sim_Event_CaveCollapse payload).
			// Uncomment once EventBus Dispatch() payload API is confirmed:
			// if (UEventBus* Bus = GetWorld()->GetSubsystem<UEventBus>())
			// {
			//     Bus->Dispatch(TAG_Sim_Event_CaveCollapse, Collapse);
			// }
		}
	}
	// ── End stability check ────────────────────────────────────────────────

	// ── Patch 2E: Remove confirmed mine overlay after successful voxel removal ──────────────
	// The player's overlay was committed using OriginalGridPosition (before ResolveMineTarget
	// adjusted Z). We remove only the single cell for this designation — no other designation
	// types or other mine cells are touched.
	{
		UWorld* OverlayWorld = GetWorld();
		UGridSelectionSubsystem* GridSS = OverlayWorld
			? OverlayWorld->GetSubsystem<UGridSelectionSubsystem>()
			: nullptr;

		const bool bOverlayRemoved = GridSS
			? GridSS->RemoveConfirmedDesignationCell(Designation->OriginalGridPosition, EGridDesignationType::Mine)
			: false;

		UE_LOG(LogTemp, Log,
			TEXT("[MiningSubsystem] [Patch2E] Mine overlay removal. DesignationID=%d Agent=%d VoxelCoord=(%d,%d,%d) ResolvedCoord=(%d,%d,%d) VoxelRemoved=true OverlayRemoved=%s"),
			DesignationID,
			AgentID,
			Designation->OriginalGridPosition.X, Designation->OriginalGridPosition.Y, Designation->OriginalGridPosition.Z,
			Designation->GridPosition.X, Designation->GridPosition.Y, Designation->GridPosition.Z,
			bOverlayRemoved ? TEXT("true") : TEXT("false"));
	}
	// ── End Patch 2E ─────────────────────────────────────────────────────────────────────

	// Brush IDs are not generated here anymore.
	Designation->AppliedBrushID = INDEX_NONE;
	Designation->State = EMiningDesignationState::Complete;
	Designation->ReservedAgentID = INDEX_NONE;

	UE_LOG(LogTemp, Warning,
		TEXT("[MiningSubsystem] Completed designation ID=%d Agent=%d Grid=%s RemovedType=%s"),
		DesignationID,
		AgentID,
		*Designation->GridPosition.ToString(),
		*Designation->ExpectedBlockType.ToString());

	return true;
}

// ===================================================================
// Designation failure
// ===================================================================

bool UMiningSubsystem::FailDesignation(int32 DesignationID, int32 AgentID)
{
	FMiningDesignation* Designation = Designations.Find(DesignationID);
	if (!Designation)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] FailDesignation failed. Missing designation ID=%d Agent=%d"),
			DesignationID,
			AgentID);
		return false;
	}

	if (Designation->ReservedAgentID != AgentID)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] FailDesignation failed. ID=%d reserved by Agent=%d but failed by Agent=%d"),
			DesignationID,
			Designation->ReservedAgentID,
			AgentID);
		return false;
	}

	Designation->State = EMiningDesignationState::Failed;
	Designation->ReservedAgentID = INDEX_NONE;

	UE_LOG(LogTemp, Warning,
		TEXT("[MiningSubsystem] Failed designation ID=%d Agent=%d"),
		DesignationID,
		AgentID);

	return true;
}

// ===================================================================
// Cave-in cancellation
// ===================================================================

void UMiningSubsystem::CancelDesignationsAtVoxel(FIntVector VoxelGrid, int32 SettlementID)
{
	for (TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		FMiningDesignation& Des = Pair.Value;

		if (Des.SettlementID != SettlementID) { continue; }
		if (IsDesignationTerminal(Des)) { continue; }
		if (Des.GridPosition != VoxelGrid) { continue; }

		Des.State = EMiningDesignationState::Cancelled;
		Des.ReservedAgentID = INDEX_NONE;

		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] Cancelled designation ID=%d at (%d,%d,%d) due to cave-in."),
			Des.DesignationID,
			VoxelGrid.X, VoxelGrid.Y, VoxelGrid.Z);
	}
}

// ===================================================================
// Queries
// ===================================================================

void UMiningSubsystem::GetPendingDesignations(
	int32 SettlementID,
	TArray<FMiningDesignation>& OutDesignations) const
{
	OutDesignations.Reset();

	for (const TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		const FMiningDesignation& Designation = Pair.Value;

		if (Designation.SettlementID == SettlementID &&
			Designation.State == EMiningDesignationState::Pending)
		{
			OutDesignations.Add(Designation);
		}
	}
}

void UMiningSubsystem::GetAllDesignations(
	int32 SettlementID,
	TArray<FMiningDesignation>& OutDesignations) const
{
	OutDesignations.Reset();

	for (const TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		const FMiningDesignation& Designation = Pair.Value;

		if (Designation.SettlementID == SettlementID)
		{
			OutDesignations.Add(Designation);
		}
	}
}

EDigBrushShape UMiningSubsystem::GetDesignationBrushShape(int32 DesignationID) const
{
	const FMiningDesignation* Designation = Designations.Find(DesignationID);
	if (!Designation) { return EDigBrushShape::Box; }
	return Designation->BrushShape;
}

void UMiningSubsystem::SetMiningRemovalMode(EMiningRemovalMode NewMode)
{
	CurrentRemovalMode = NewMode;

	UE_LOG(LogTemp, Log,
		TEXT("[MiningSubsystem] Mining removal mode set to %d"),
		static_cast<int32>(CurrentRemovalMode));
}

EMiningRemovalMode UMiningSubsystem::ToggleMiningRemovalMode()
{
	CurrentRemovalMode =
		(CurrentRemovalMode == EMiningRemovalMode::VoxelCube)
		? EMiningRemovalMode::SDFBrush
		: EMiningRemovalMode::VoxelCube;

	UE_LOG(LogTemp, Log,
		TEXT("[MiningSubsystem] Mining removal mode toggled to %d"),
		static_cast<int32>(CurrentRemovalMode));

	return CurrentRemovalMode;
}

FText UMiningSubsystem::GetMiningRemovalModeText() const
{
	switch (CurrentRemovalMode)
	{
	case EMiningRemovalMode::VoxelCube: return FText::FromString(TEXT("Mining: Voxel Cube"));
	case EMiningRemovalMode::SDFBrush:  return FText::FromString(TEXT("Mining: SDF Brush"));
	default:                            return FText::FromString(TEXT("Mining: Unknown"));
	}
}
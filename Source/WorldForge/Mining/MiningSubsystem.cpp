#include "Mining/MiningSubsystem.h"

#include "Voxel/VoxelWorldManager.h"
#include "Colony/SettlementManager.h"
#include "Colony/ProcessorManager.h"
#include "Colony/Processors/JobProcessor.h"
#include "Data/WorldForgeTags.h"
#include "Mining/StabilitySubsystem.h"
#include "Engine/World.h"

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
		Designation.State == EMiningDesignationState::Complete  ||
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

			return true;
		}
	}

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
	OutDesignationID = INDEX_NONE;

	FGameplayTag BlockType;
	FIntVector ResolvedGridPosition;

	if (!ResolveMineTarget(GridPosition, ResolvedGridPosition, BlockType))
	{
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
			int32 WorldTileID = INDEX_NONE;
			FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
			int32 ResolvedPlayerSettlementID = INDEX_NONE;
			int32 ClaimedWorldTileCount = 0;
			if (PlayerSettlementID != INDEX_NONE &&
				SettlementID == PlayerSettlementID &&
				!SettlementManager->DoesPlayerSettlementClaimVoxelCellDetailed(
					FIntPoint(ResolvedGridPosition.X, ResolvedGridPosition.Y),
					WorldTileID,
					WorldTileXY,
					ResolvedPlayerSettlementID,
					ClaimedWorldTileCount))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MiningSubsystem] CreateMiningDesignation blocked: unclaimed land. Settlement=%d Voxel=(%d,%d,%d) WorldTile=(%d,%d) WorldTileID=%d ClaimedTileCount=%d"),
					SettlementID,
					ResolvedGridPosition.X,
					ResolvedGridPosition.Y,
					ResolvedGridPosition.Z,
					WorldTileXY.X,
					WorldTileXY.Y,
					WorldTileID,
					ClaimedWorldTileCount);
				return false;
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

			return false;
		}
	}

	FMiningDesignation NewDesignation;
	NewDesignation.DesignationID    = NextDesignationID++;
	NewDesignation.SettlementID     = SettlementID;
	NewDesignation.GridPosition     = ResolvedGridPosition;
	NewDesignation.ExpectedBlockType= BlockType;
	NewDesignation.State            = EMiningDesignationState::Pending;
	NewDesignation.ReservedAgentID  = INDEX_NONE;
	NewDesignation.CreatedStep      = CurrentStep;
	NewDesignation.BrushShape       = EDigBrushShape::Box;
	NewDesignation.RemovalMode      = CurrentRemovalMode;
	NewDesignation.AppliedBrushID   = INDEX_NONE;

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

	Designation->State          = EMiningDesignationState::Cancelled;
	Designation->ReservedAgentID= INDEX_NONE;

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
	OutGridPosition  = FIntVector::ZeroValue;

	// Cache subsystem pointers once outside the loop.
	UVoxelWorldManager*   VM      = GetVoxelWorldManager();
	UStabilitySubsystem*  StabSys = GetWorld() ? GetWorld()->GetSubsystem<UStabilitySubsystem>() : nullptr;

	for (TPair<int32, FMiningDesignation>& Pair : Designations)
	{
		FMiningDesignation& Designation = Pair.Value;

		if (Designation.SettlementID != SettlementID)   { continue; }
		if (Designation.State != EMiningDesignationState::Pending) { continue; }

		FGameplayTag CurrentBlockType;
		FIntVector   ResolvedGridPosition;

		if (!ResolveMineTarget(Designation.GridPosition, ResolvedGridPosition, CurrentBlockType))
		{
			Designation.State          = EMiningDesignationState::Failed;
			Designation.ReservedAgentID= INDEX_NONE;

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
			const int32 SurfaceZ          = VM->GetSurfaceZAt(FIntPoint(ResolvedGridPosition.X, ResolvedGridPosition.Y));
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

		Designation.GridPosition     = ResolvedGridPosition;
		Designation.ExpectedBlockType= CurrentBlockType;
		Designation.State            = EMiningDesignationState::Reserved;
		Designation.ReservedAgentID  = AgentID;

		OutDesignationID = Designation.DesignationID;
		OutGridPosition  = Designation.GridPosition;

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
			Designation.State          = EMiningDesignationState::Failed;
			Designation.ReservedAgentID= INDEX_NONE;

			UE_LOG(LogTemp, Warning,
				TEXT("[MiningSubsystem] PostMiningJobs failed validation. ID=%d Grid=%s"),
				Designation.DesignationID,
				*Designation.GridPosition.ToString());

			continue;
		}

		Designation.GridPosition     = ResolvedGridPosition;
		Designation.ExpectedBlockType= CurrentBlockType;

		FJobRequest Req;
		Req.SettlementID  = SettlementID;
		Req.JobTag        = TAG_Job_Mine;
		Req.RequiredSkill = TAG_Skill_Mining;
		Req.Priority      = EJobPriority::High;
		Req.PostedOnStep  = StepNumber;

		// Critical metadata:
		// TargetID = Mining DesignationID
		// TargetGridPos = X/Y nav target for the miner
		Req.TargetID      = Designation.DesignationID;
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
		Designation->State          = EMiningDesignationState::Failed;
		Designation->ReservedAgentID= INDEX_NONE;

		UE_LOG(LogTemp, Warning,
			TEXT("[MiningSubsystem] CompleteDesignation failed validation. ID=%d Grid=%s"),
			DesignationID,
			*Designation->GridPosition.ToString());

		return false;
	}

	Designation->GridPosition     = ResolvedGridPosition;
	Designation->ExpectedBlockType= CurrentBlockType;

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
		Designation->State          = EMiningDesignationState::Failed;
		Designation->ReservedAgentID= INDEX_NONE;

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

	// Brush IDs are not generated here anymore.
	Designation->AppliedBrushID  = INDEX_NONE;
	Designation->State           = EMiningDesignationState::Complete;
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

	Designation->State          = EMiningDesignationState::Failed;
	Designation->ReservedAgentID= INDEX_NONE;

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

		Des.State          = EMiningDesignationState::Cancelled;
		Des.ReservedAgentID= INDEX_NONE;

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

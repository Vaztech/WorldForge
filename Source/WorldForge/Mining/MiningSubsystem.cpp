#include "Mining/MiningSubsystem.h"

#include "VoxelWorldManager.h"
#include "Engine/World.h"

void UMiningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    VoxelWorldManager = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[MiningSubsystem] Initialized."));
}

void UMiningSubsystem::Deinitialize()
{
    Designations.Reset();
    VoxelWorldManager = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[MiningSubsystem] Deinitialized."));

    Super::Deinitialize();
}

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
        return true;
    }

    // Surface raycasts can land slightly above the actual solid voxel.
    // Search downward first.
    for (int32 OffsetZ = 1; OffsetZ <= 12; ++OffsetZ)
    {
        const FIntVector ProbePos(
            GridPosition.X,
            GridPosition.Y,
            GridPosition.Z - OffsetZ);

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

    // Safety: sometimes the grid target can be slightly below/offset.
    for (int32 OffsetZ = 1; OffsetZ <= 4; ++OffsetZ)
    {
        const FIntVector ProbePos(
            GridPosition.X,
            GridPosition.Y,
            GridPosition.Z + OffsetZ);

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
    NewDesignation.DesignationID = NextDesignationID++;
    NewDesignation.SettlementID = SettlementID;
    NewDesignation.GridPosition = ResolvedGridPosition;
    NewDesignation.ExpectedBlockType = BlockType;
    NewDesignation.State = EMiningDesignationState::Pending;
    NewDesignation.ReservedAgentID = INDEX_NONE;
    NewDesignation.CreatedStep = CurrentStep;

    Designations.Add(NewDesignation.DesignationID, NewDesignation);
    OutDesignationID = NewDesignation.DesignationID;

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningSubsystem] Created designation ID=%d Settlement=%d Grid=%s Type=%s Step=%d"),
        OutDesignationID,
        SettlementID,
        *ResolvedGridPosition.ToString(),
        *BlockType.ToString(),
        CurrentStep);

    return true;
}

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

bool UMiningSubsystem::TryReserveDesignation(
    int32 SettlementID,
    int32 AgentID,
    int32& OutDesignationID,
    FIntVector& OutGridPosition)
{
    OutDesignationID = INDEX_NONE;
    OutGridPosition = FIntVector::ZeroValue;

    for (TPair<int32, FMiningDesignation>& Pair : Designations)
    {
        FMiningDesignation& Designation = Pair.Value;

        if (Designation.SettlementID != SettlementID)
        {
            continue;
        }

        if (Designation.State != EMiningDesignationState::Pending)
        {
            continue;
        }

        FGameplayTag CurrentBlockType;
        FIntVector ResolvedGridPosition;

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

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningSubsystem] CompleteDesignation removing block. ID=%d Agent=%d Grid=%s ExpectedType=%s"),
        DesignationID,
        AgentID,
        *Designation->GridPosition.ToString(),
        *Designation->ExpectedBlockType.ToString());

    const bool bRemoved = VM->RemoveBlockAt(Designation->GridPosition);

    if (!bRemoved)
    {
        Designation->State = EMiningDesignationState::Failed;
        Designation->ReservedAgentID = INDEX_NONE;

        UE_LOG(LogTemp, Error,
            TEXT("[MiningSubsystem] CompleteDesignation failed to remove block. ID=%d Agent=%d Grid=%s"),
            DesignationID,
            AgentID,
            *Designation->GridPosition.ToString());

        return false;
    }

    Designation->State = EMiningDesignationState::Complete;
    Designation->ReservedAgentID = INDEX_NONE;

    UE_LOG(LogTemp, Warning,
        TEXT("[MiningSubsystem] Completed designation ID=%d Agent=%d Grid=%s"),
        DesignationID,
        AgentID,
        *Designation->GridPosition.ToString());

    return true;
}

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
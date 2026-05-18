#include "Civilization/CivilizationActivitySubsystem.h"

#include "Colony/SettlementManager.h"
#include "Colony/ProcessorManager.h"
#include "Colony/SimulationTimeManager.h"
#include "Farming/FarmingSubsystem.h"
#include "Farming/FarmingTypes.h"
#include "Mining/MiningSubsystem.h"
#include "Voxel/VoxelConstructionSystem.h"
#include "Voxel/VoxelWorldManager.h"
#include "Data/WorldForgeTags.h"
#include "Engine/World.h"

void UCivilizationActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[CivActivity] Initialized."));
}

void UCivilizationActivitySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    CacheSubsystems();

    if (SimulationTimeManager)
    {
        SimulationTimeManager->OnSimulationStep.AddDynamic(this, &UCivilizationActivitySubsystem::TickCivilizationActivity);
        UE_LOG(LogTemp, Log, TEXT("[CivActivity] Bound to SimulationTimeManager."));
    }
}

void UCivilizationActivitySubsystem::Deinitialize()
{
    if (SimulationTimeManager)
    {
        SimulationTimeManager->OnSimulationStep.RemoveDynamic(this, &UCivilizationActivitySubsystem::TickCivilizationActivity);
    }

    SettlementManager = nullptr;
    ProcessorManager = nullptr;
    FarmingSubsystem = nullptr;
    MiningSubsystem = nullptr;
    ConstructionSystem = nullptr;
    VoxelWorldManager = nullptr;
    SimulationTimeManager = nullptr;
    CivToSettlementID.Reset();
    SeededFarmSettlements.Reset();
    SeededInitialBuildingSettlements.Reset();
    LastMiningPlanStepBySettlement.Reset();
    LastBuildPlanStepBySettlement.Reset();
    MiningPlanCursorBySettlement.Reset();
    bInitializedFromWorld = false;

    Super::Deinitialize();
}

void UCivilizationActivitySubsystem::CacheSubsystems()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    SettlementManager = World->GetSubsystem<USettlementManager>();
    ProcessorManager = World->GetSubsystem<UProcessorManager>();
    FarmingSubsystem = World->GetSubsystem<UFarmingSubsystem>();
    MiningSubsystem = World->GetSubsystem<UMiningSubsystem>();
    ConstructionSystem = World->GetSubsystem<UVoxelConstructionSystem>();
    VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    SimulationTimeManager = World->GetSubsystem<USimulationTimeManager>();
}

bool UCivilizationActivitySubsystem::IsValidTileIndex(int32 TileIndex, int32 TileCount) const
{
    return TileIndex >= 0 && TileIndex < TileCount;
}

FIntPoint UCivilizationActivitySubsystem::TileIndexToCell(int32 TileIndex, int32 Width) const
{
    if (Width <= 0)
    {
        return FIntPoint::ZeroValue;
    }

    return FIntPoint(TileIndex % Width, TileIndex / Width);
}

FGameplayTag UCivilizationActivitySubsystem::ResolveFallbackRaceByIndex(int32 CivIndex) const
{
    switch (CivIndex % 3)
    {
    case 0: return TAG_Race_Dwarf;
    case 1: return TAG_Race_HighElf;
    default: return TAG_Race_Human;
    }
}

FGameplayTag UCivilizationActivitySubsystem::ResolveCivRace(const FCivilizationData& Civ) const
{
    return Civ.RaceType.IsValid() ? Civ.RaceType : ResolveFallbackRaceByIndex(Civ.CivID);
}

void UCivilizationActivitySubsystem::InitializeFromGeneratedWorld(
    const TArray<FCivilizationData>& Civs,
    const TArray<FWorldTileData>& Tiles,
    int32 GridWidth,
    int32 GridHeight,
    bool bForceRebuild)
{
    if (!bCreateNPCSettlements)
    {
        UE_LOG(LogTemp, Log, TEXT("[CivActivity] NPC settlement creation disabled."));
        return;
    }

    if (bInitializedFromWorld && !bForceRebuild)
    {
        UE_LOG(LogTemp, Log, TEXT("[CivActivity] Already initialized from world. CivSettlements=%d"), CivToSettlementID.Num());
        return;
    }

    CacheSubsystems();

    if (!SettlementManager || !ProcessorManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CivActivity] Cannot initialize NPC civs: SettlementManager=%d ProcessorManager=%d"), SettlementManager != nullptr, ProcessorManager != nullptr);
        return;
    }

    CivToSettlementID.Reset();

    for (int32 CivIndex = 0; CivIndex < Civs.Num(); ++CivIndex)
    {
        const FCivilizationData& Civ = Civs[CivIndex];

        if (Civ.bCollapsed)
        {
            continue;
        }

        if (!IsValidTileIndex(Civ.StartTileIndex, Tiles.Num()))
        {
            UE_LOG(LogTemp, Warning, TEXT("[CivActivity] Skipping CivID=%d: invalid StartTileIndex=%d"), Civ.CivID, Civ.StartTileIndex);
            continue;
        }

        const FIntPoint StartCell = TileIndexToCell(Civ.StartTileIndex, GridWidth);
        SeedNPCSettlement(Civ, StartCell, CivIndex);
    }

    bInitializedFromWorld = true;

    UE_LOG(LogTemp, Log, TEXT("[CivActivity] Initialized %d active NPC civilization settlements from %d civs."), CivToSettlementID.Num(), Civs.Num());
}

void UCivilizationActivitySubsystem::SeedNPCSettlement(const FCivilizationData& Civ, const FIntPoint& StartCell, int32 CivIndex)
{
    if (!SettlementManager || !ProcessorManager)
    {
        return;
    }

    const FString CivName = Civ.Name.IsEmpty()
        ? FString::Printf(TEXT("Civilization_%d"), Civ.CivID)
        : Civ.Name;

    const FString SettlementName = FString::Printf(TEXT("%s Capital"), *CivName);
    const int32 SettlementID = SettlementManager->CreateSettlement(SettlementName, Civ.StartTileIndex);

    FSettlementState* State = SettlementManager->GetMutableSettlement(SettlementID);
    if (!State)
    {
        return;
    }

    State->OwnerCivID = Civ.CivID;
    State->WorldOrigin = StartCell + SettlementOriginOffset;
    State->PlayerCurrencyTag = Civ.CurrencyTag.IsValid() ? Civ.CurrencyTag : TAG_Currency_Barter;

    ProcessorManager->RegisterSettlement(SettlementID);
    CivToSettlementID.Add(Civ.CivID, SettlementID);

    SeedNPCStockpile(SettlementID);
    SeedNPCBuildings(SettlementID);
    SeedNPCAgents(SettlementID, Civ, State->WorldOrigin, CivIndex);
    SeedNPCFarmZone(SettlementID);

    SettlementManager->SetVisualDirty(SettlementID);

    UE_LOG(LogTemp, Log,
        TEXT("[CivActivity] Created active NPC settlement. CivID=%d SettlementID=%d Race=%s StartCell=(%d,%d) Origin=(%d,%d)"),
        Civ.CivID,
        SettlementID,
        *ResolveCivRace(Civ).ToString(),
        StartCell.X,
        StartCell.Y,
        State->WorldOrigin.X,
        State->WorldOrigin.Y);
}

void UCivilizationActivitySubsystem::SeedNPCStockpile(int32 SettlementID)
{
    if (!SettlementManager)
    {
        return;
    }

    SettlementManager->AddToStockpile(SettlementID, TAG_Item_Food, 250);
    SettlementManager->AddToStockpile(SettlementID, TAG_Item_Wood, 250);
    SettlementManager->AddToStockpile(SettlementID, TAG_Item_Stone, 250);

    // FarmingSubsystem seed gates use these tags for category planting.
    SettlementManager->AddToStockpile(SettlementID, FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Grain")), false), 50);
    SettlementManager->AddToStockpile(SettlementID, FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.RootVegetable")), false), 50);
}

void UCivilizationActivitySubsystem::SeedNPCBuildings(int32 SettlementID)
{
    if (!SettlementManager || SeededInitialBuildingSettlements.Contains(SettlementID))
    {
        return;
    }

    // Initial placeholder infrastructure. Future planner phases can replace
    // these direct completions with actual construction jobs per civ.
    SettlementManager->AddBuilding(SettlementID, TAG_Building_Housing_Hut);
    SettlementManager->AddBuilding(SettlementID, TAG_Building_Storage_Stockpile);

    SeededInitialBuildingSettlements.Add(SettlementID);
}

void UCivilizationActivitySubsystem::SeedNPCFarmZone(int32 SettlementID)
{
    if (!FarmingSubsystem || SeededFarmSettlements.Contains(SettlementID))
    {
        return;
    }

    const int32 ZoneID = FarmingSubsystem->CreateFarmZone(
        SettlementID,
        FIntPoint(4, 4),
        NPCFarmSize,
        NPCFarmSize,
        ECropCategory::Grains,
        FName(TEXT("LP_Wheat")),
        true);

    if (ZoneID != INDEX_NONE)
    {
        SeededFarmSettlements.Add(SettlementID);
        UE_LOG(LogTemp, Log, TEXT("[CivActivity] Created starter farm zone. Settlement=%d Zone=%d Size=%dx%d"), SettlementID, ZoneID, NPCFarmSize, NPCFarmSize);
    }
}

void UCivilizationActivitySubsystem::ApplyRaceSkillDefaults(FAgentData& Agent) const
{
    Agent.Skills.Add(TAG_Skill_Farming, 35.f);
    Agent.Skills.Add(TAG_Skill_Hauling, 35.f);
    Agent.Skills.Add(TAG_Skill_Mining, 35.f);
    Agent.Skills.Add(TAG_Skill_Cooking, 25.f);
    Agent.Skills.Add(TAG_Skill_Crafting, 25.f);
    Agent.Skills.Add(TAG_Skill_Building, 35.f);
    Agent.Skills.Add(TAG_Skill_Research, 20.f);
    Agent.Skills.Add(TAG_Skill_Medicine, 20.f);
    Agent.Skills.Add(TAG_Skill_Combat, 20.f);

    if (Agent.RaceTag == TAG_Race_Dwarf)
    {
        Agent.Skills.FindOrAdd(TAG_Skill_Mining) += 15.f;
        Agent.Skills.FindOrAdd(TAG_Skill_Crafting) += 10.f;
        Agent.MaxHealth = 110.f;
        Agent.Health = Agent.MaxHealth;
    }
    else if (Agent.RaceTag == TAG_Race_HighElf)
    {
        Agent.Skills.FindOrAdd(TAG_Skill_Farming) += 10.f;
        Agent.Skills.FindOrAdd(TAG_Skill_Research) += 15.f;
    }

    for (TPair<FGameplayTag, float>& Pair : Agent.Skills)
    {
        Pair.Value = FMath::Clamp(Pair.Value, 0.f, 100.f);
    }
}

void UCivilizationActivitySubsystem::SetAllKnownJobPrioritiesHigh(FAgentData& Agent) const
{
    const TArray<FGameplayTag> JobTags = {
        TAG_Job_Eat,
        TAG_Job_Sleep,
        TAG_Job_Haul,
        TAG_Job_Build,
        TAG_Job_Mine,
        TAG_Job_Harvest_Food,
        TAG_Job_Harvest_Wood,
        TAG_Job_Harvest_Stone,
        TAG_Job_Cook,
        TAG_Job_Craft,
        TAG_Job_Farm_PrepSoil,
        TAG_Job_Farm_Plant,
        TAG_Job_Farm_Water,
        TAG_Job_Farm_Harvest,
        TAG_Job_Farm_Clear,
        TAG_Job_Farm_Fertilize
    };

    for (const FGameplayTag& JobTag : JobTags)
    {
        if (JobTag.IsValid())
        {
            Agent.JobPriorities.FindOrAdd(JobTag) = 4;
        }
    }
}

void UCivilizationActivitySubsystem::SeedNPCAgents(int32 SettlementID, const FCivilizationData& Civ, const FIntPoint& WorldOrigin, int32 CivIndex)
{
    if (!ProcessorManager || !SettlementManager)
    {
        return;
    }

    const FGameplayTag RaceTag = ResolveCivRace(Civ);

    for (int32 AgentIndex = 0; AgentIndex < NPCAgentsPerCivilization; ++AgentIndex)
    {
        FAgentData Agent;
        Agent.AgentID = 100000 + (Civ.CivID * 1000) + AgentIndex;
        Agent.SettlementID = SettlementID;
        Agent.DisplayName = FString::Printf(TEXT("%s Worker %d"), Civ.Name.IsEmpty() ? TEXT("Civ") : *Civ.Name, AgentIndex + 1);
        Agent.State = EAgentState::Idle;
        Agent.RaceTag = RaceTag;
        Agent.FoodNeed = 100.f;
        Agent.SleepNeed = 100.f;
        Agent.SocialNeed = 70.f;
        Agent.SafetyNeed = 70.f;
        Agent.PurposeNeed = 70.f;
        Agent.MoodScore = 50.f;
        Agent.Health = 100.f;
        Agent.MaxHealth = 100.f;
        Agent.CurrentGridPos = FIntPoint(AgentIndex % 2, AgentIndex / 2);
        Agent.CurrentNavTarget = Agent.CurrentGridPos;
        Agent.ActiveReservationID = INDEX_NONE;

        ApplyRaceSkillDefaults(Agent);
        SetAllKnownJobPrioritiesHigh(Agent);

        ProcessorManager->RegisterAgent(Agent);
        SettlementManager->IncrementPopulation(SettlementID);
        SettlementManager->IncrementRacePopulation(SettlementID, RaceTag);
    }
}

void UCivilizationActivitySubsystem::TickCivilizationActivity(int32 StepNumber)
{
    if (!bEnableActivitySimulation || !bInitializedFromWorld)
    {
        return;
    }

    if (ActivityIntervalSteps <= 0 || (StepNumber % ActivityIntervalSteps) != 0)
    {
        return;
    }

    CacheSubsystems();

    for (const TPair<int32, int32>& Pair : CivToSettlementID)
    {
        EnsureActivityForSettlement(Pair.Key, Pair.Value, StepNumber);
    }
}

void UCivilizationActivitySubsystem::EnsureActivityForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber)
{
    if (!SettlementManager)
    {
        return;
    }

    const int32 FoodAmount = SettlementManager->GetStockpileAmount(SettlementID, TAG_Item_Food);
    const int32 WoodAmount = SettlementManager->GetStockpileAmount(SettlementID, TAG_Item_Wood);
    const int32 StoneAmount = SettlementManager->GetStockpileAmount(SettlementID, TAG_Item_Stone);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[CivActivity] Think CivID=%d Settlement=%d Step=%d Food=%d Wood=%d Stone=%d"),
        CivID,
        SettlementID,
        StepNumber,
        FoodAmount,
        WoodAmount,
        StoneAmount);

    // Keep NPC civs supplied enough to exercise their work loops while the
    // deeper trade/logistics economy comes online.
    if ((StepNumber % (ActivityIntervalSteps * 2)) == 0)
    {
        SettlementManager->AddToStockpile(SettlementID, TAG_Item_Food, 5);
        SettlementManager->AddToStockpile(SettlementID, TAG_Item_Wood, 5);
        SettlementManager->AddToStockpile(SettlementID, TAG_Item_Stone, 5);

        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[CivActivity] Maintenance supply trickle. Settlement=%d +5 Food/Wood/Stone Step=%d"),
            SettlementID,
            StepNumber);
    }

    EnsureBuildingPlanForSettlement(CivID, SettlementID, StepNumber);
    EnsureMiningPlanForSettlement(CivID, SettlementID, StepNumber);
    EnsureFarmPlanForSettlement(CivID, SettlementID, StepNumber);
}

void UCivilizationActivitySubsystem::EnsureBuildingPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber)
{
    if (!SettlementManager)
    {
        return;
    }

    const int32* LastStep = LastBuildPlanStepBySettlement.Find(SettlementID);
    if (LastStep && (StepNumber - *LastStep) < ActivityIntervalSteps)
    {
        return;
    }

    // Simple staged settlement growth. The VSS reads CompletedBuildings and
    // turns them into visuals. This is intentionally data/tag driven so more
    // building/job types can be appended later without changing settlement core.
    const TArray<FGameplayTag> BuildOrder = {
        TAG_Building_Production_Kitchen,
        TAG_Building_Production_Carpenter,
        TAG_Building_Economic_MarketStall,
        TAG_Building_Spiritual_Shrine,
        TAG_Building_Defense_Barracks
    };

    for (const FGameplayTag& BuildingTag : BuildOrder)
    {
        if (BuildingTag.IsValid() && !SettlementManager->HasBuilding(SettlementID, BuildingTag))
        {
            SettlementManager->AddBuilding(SettlementID, BuildingTag);
            LastBuildPlanStepBySettlement.FindOrAdd(SettlementID) = StepNumber;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[CivActivity] BuildPlan CivID=%d Settlement=%d chose=%s Step=%d"),
                CivID,
                SettlementID,
                *BuildingTag.ToString(),
                StepNumber);
            return;
        }
    }

    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[CivActivity] BuildPlan CivID=%d Settlement=%d skipped: all starter buildings already present. Step=%d"),
        CivID,
        SettlementID,
        StepNumber);
}

void UCivilizationActivitySubsystem::EnsureFarmPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber)
{
    if (!FarmingSubsystem)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[CivActivity] FarmPlan skipped: FarmingSubsystem=null Settlement=%d Step=%d"), SettlementID, StepNumber);
        return;
    }

    // The starter farm zone already posts Prep/Plant/Water/Harvest jobs from FarmingSubsystem::TickFarmZones.
    // Keep this log here so it is obvious that farming is owned by the farm subsystem, not faked here.
    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[CivActivity] FarmPlan CivID=%d Settlement=%d active. FarmingSubsystem owns farm job posting. Step=%d"),
        CivID,
        SettlementID,
        StepNumber);
}

void UCivilizationActivitySubsystem::EnsureMiningPlanForSettlement(int32 CivID, int32 SettlementID, int32 StepNumber)
{
    if (!MiningSubsystem)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[CivActivity] MinePlan skipped: MiningSubsystem=null Settlement=%d Step=%d"), SettlementID, StepNumber);
        return;
    }

    const int32* LastStep = LastMiningPlanStepBySettlement.Find(SettlementID);
    if (LastStep && (StepNumber - *LastStep) < MiningPlanIntervalSteps)
    {
        return;
    }

    TArray<FMiningDesignation> ExistingDesignations;
    MiningSubsystem->GetAllDesignations(SettlementID, ExistingDesignations);

    int32 OpenDesignationCount = 0;
    for (const FMiningDesignation& Existing : ExistingDesignations)
    {
        if (Existing.State == EMiningDesignationState::Pending || Existing.State == EMiningDesignationState::Reserved)
        {
            ++OpenDesignationCount;
        }
    }

    if (OpenDesignationCount >= MaxOpenMiningDesignationsPerSettlement)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[CivActivity] MinePlan skipped: Settlement=%d already has %d open mining designations. Step=%d"),
            SettlementID,
            OpenDesignationCount,
            StepNumber);
        return;
    }

    const FSettlementState* Settlement = SettlementManager ? SettlementManager->GetSettlement(SettlementID) : nullptr;
    if (!Settlement)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CivActivity] MinePlan failed: missing settlement %d"), SettlementID);
        return;
    }

    int32 CreatedCount = 0;
    int32& Cursor = MiningPlanCursorBySettlement.FindOrAdd(SettlementID);

    const int32 Attempts = MiningSearchRadiusCells * MiningSearchRadiusCells * 2;
    for (int32 Attempt = 0; Attempt < Attempts && CreatedCount < MiningDesignationsPerPlan; ++Attempt)
    {
        FIntVector CandidateVoxel = FIntVector::ZeroValue;
        if (!FindMiningCandidateNearSettlement(*Settlement, Cursor + Attempt, CandidateVoxel))
        {
            continue;
        }

        int32 DesignationID = INDEX_NONE;
        if (MiningSubsystem->CreateMiningDesignation(SettlementID, CandidateVoxel, StepNumber, DesignationID))
        {
            ++CreatedCount;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[CivActivity] MinePlan CivID=%d Settlement=%d created designation=%d Voxel=(%d,%d,%d) Step=%d"),
                CivID,
                SettlementID,
                DesignationID,
                CandidateVoxel.X,
                CandidateVoxel.Y,
                CandidateVoxel.Z,
                StepNumber);
        }
    }

    Cursor += Attempts;

    if (CreatedCount > 0)
    {
        LastMiningPlanStepBySettlement.FindOrAdd(SettlementID) = StepNumber;
        MiningSubsystem->PostMiningJobs(SettlementID, StepNumber);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[CivActivity] MinePlan CivID=%d Settlement=%d posted jobs for %d new designation(s). Step=%d"),
            CivID,
            SettlementID,
            CreatedCount,
            StepNumber);
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[CivActivity] MinePlan CivID=%d Settlement=%d created no mining designations. Origin=(%d,%d) Radius=%d Step=%d"),
            CivID,
            SettlementID,
            Settlement->WorldOrigin.X,
            Settlement->WorldOrigin.Y,
            MiningSearchRadiusCells,
            StepNumber);
    }
}

bool UCivilizationActivitySubsystem::FindMiningCandidateNearSettlement(
    const FSettlementState& Settlement,
    int32 SearchSeed,
    FIntVector& OutVoxel) const
{
    if (!VoxelWorldManager)
    {
        return false;
    }

    const int32 Radius = FMath::Max(1, MiningSearchRadiusCells);
    const int32 Diameter = Radius * 2 + 1;
    const int32 Wrapped = FMath::Abs(SearchSeed) % FMath::Max(1, Diameter * Diameter);

    const int32 LocalX = (Wrapped % Diameter) - Radius;
    const int32 LocalY = (Wrapped / Diameter) - Radius;

    const FIntPoint CandidateCell = Settlement.WorldOrigin + FIntPoint(LocalX, LocalY);

    int32 SurfaceZ = 0;
    if (!VoxelWorldManager->TryGetTerrainSurfaceVoxelZ(CandidateCell, SurfaceZ))
    {
        return false;
    }

    // Pick the surface or just-below-surface voxel. MiningSubsystem will probe and correct
    // the exact block if the ray/surface is slightly above the solid voxel.
    OutVoxel = FIntVector(CandidateCell.X, CandidateCell.Y, SurfaceZ);
    return true;
}

#include "ColonistVisualManager.h"

#include "WorldForgeColonist.h"
#include "ProcessorManager.h"
#include "SimulationTimeManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/DataTable.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPath.h"
#include "VoxelWorldManager.h"


namespace
{
    FString WF_NormalizeToken(FString In)
    {
        In.ReplaceInline(TEXT("_"), TEXT(""));
        In.ReplaceInline(TEXT(" "), TEXT(""));
        In.ReplaceInline(TEXT("-"), TEXT(""));
        return In.ToLower();
    }

    FString WF_RaceTagToVisualRowPrefix(FGameplayTag RaceTag)
    {
        FString RaceLeaf;
        const FString RaceString = RaceTag.ToString();
        if (!RaceString.Split(TEXT("."), nullptr, &RaceLeaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            RaceLeaf = RaceString;
        }

        const FString Normalized = WF_NormalizeToken(RaceLeaf);

        if (Normalized == TEXT("construct") || Normalized == TEXT("constructfolk"))
        {
            return TEXT("ConstructFolk");
        }
        if (Normalized == TEXT("darkelf"))
        {
            return TEXT("DarkElf");
        }
        if (Normalized == TEXT("faekin") || Normalized == TEXT("fae"))
        {
            return TEXT("FaeKin");
        }
        if (Normalized == TEXT("highelf"))
        {
            return TEXT("HighElf");
        }
        if (Normalized == TEXT("namekian"))
        {
            return TEXT("Namekian");
        }
        if (Normalized == TEXT("tidecaller") || Normalized == TEXT("tidcealler") || Normalized == TEXT("tidcaller"))
        {
            return TEXT("Tidecaller");
        }
        if (Normalized == TEXT("dwarf"))
        {
            return TEXT("Dwarf");
        }
        if (Normalized == TEXT("oni"))
        {
            return TEXT("Oni");
        }

        // Last-resort display conversion for future/modded races.
        FString Result;
        bool bCapNext = true;
        for (TCHAR C : RaceLeaf)
        {
            if (C == TEXT('_') || C == TEXT('-') || C == TEXT(' '))
            {
                bCapNext = true;
                continue;
            }

            Result.AppendChar(bCapNext ? FChar::ToUpper(C) : C);
            bCapNext = false;
        }

        return Result.IsEmpty() ? RaceLeaf : Result;
    }

    bool WF_FindVisualRow(const UDataTable* Table, FName RowName, FWorldForgeColonistVisualRow& OutDefinition, const TCHAR* Context)
    {
        if (!Table || RowName.IsNone())
        {
            return false;
        }

        if (const FWorldForgeColonistVisualRow* Found = Table->FindRow<FWorldForgeColonistVisualRow>(RowName, Context, false))
        {
            OutDefinition = *Found;
            return true;
        }

        return false;
    }

    bool WF_FindFirstMatchingRow(
        const UDataTable* Table,
        const TArray<FName>& RowNames,
        const TArray<FString>& MatchTokens,
        FName& OutRowName,
        FWorldForgeColonistVisualRow& OutDefinition,
        const TCHAR* Context)
    {
        if (!Table)
        {
            return false;
        }

        for (const FName& RowName : RowNames)
        {
            const FString RowString = RowName.ToString();
            const FString NormalizedRow = WF_NormalizeToken(RowString);

            bool bMatchesAll = true;
            for (const FString& Token : MatchTokens)
            {
                if (!NormalizedRow.Contains(WF_NormalizeToken(Token)))
                {
                    bMatchesAll = false;
                    break;
                }
            }

            if (!bMatchesAll)
            {
                continue;
            }

            if (const FWorldForgeColonistVisualRow* Found = Table->FindRow<FWorldForgeColonistVisualRow>(RowName, Context, false))
            {
                OutRowName = RowName;
                OutDefinition = *Found;
                return true;
            }
        }

        return false;
    }
}

void UColonistVisualManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UProcessorManager>();
    Collection.InitializeDependency<USimulationTimeManager>();

    Super::Initialize(Collection);

    if (!ColonistActorClass)
    {
        ColonistActorClass = AWorldForgeColonist::StaticClass();

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonistVisualManager] Using native AWorldForgeColonist as primary spawn class.")
        );
    }

    NativeFallbackVisual.DisplayName = FText::FromString(TEXT("Native Debug Fallback"));
    NativeFallbackVisual.RelativeScale = FVector(5.0f, 5.0f, 5.0f);
    NativeFallbackVisual.DebugMeshScale = FVector(0.35f, 0.35f, 0.8f);
    NativeFallbackVisual.DebugColor = FLinearColor(0.2f, 0.8f, 1.0f, 1.0f);
    NativeFallbackVisual.bUseDebugMeshWhenNoAnimation = true;

    if (VisualRegistryAsset)
    {
        LoadVisualRegistryAsset(VisualRegistryAsset);
    }
    else if (RaceVisualTables.Num() == 0)
    {
        LoadDefaultVisualRegistryAsset();
    }

    UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Initialized. RaceVisualTables=%d"), RaceVisualTables.Num());
}

void UColonistVisualManager::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    CachedProcessorManager = InWorld.GetSubsystem<UProcessorManager>();
    CachedSimulationTimeManager = InWorld.GetSubsystem<USimulationTimeManager>();
    CachedVoxelWorldManager = InWorld.GetSubsystem<UVoxelWorldManager>();

    if (CachedSimulationTimeManager)
    {
        CachedSimulationTimeManager->OnSimulationStep.AddDynamic(
            this,
            &UColonistVisualManager::HandleSimulationStep
        );

        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Bound to SimulationTimeManager."));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] SimulationTimeManager not found."));
    }

    if (CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager->OnTerrainReady.AddDynamic(
            this,
            &UColonistVisualManager::HandleTerrainReady
        );
        bBoundToTerrainReady = true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] VoxelWorldManager not found."));
    }
}

void UColonistVisualManager::Deinitialize()
{
    if (CachedSimulationTimeManager)
    {
        CachedSimulationTimeManager->OnSimulationStep.RemoveDynamic(
            this,
            &UColonistVisualManager::HandleSimulationStep
        );
    }

    if (CachedVoxelWorldManager && bBoundToTerrainReady)
    {
        CachedVoxelWorldManager->OnTerrainReady.RemoveDynamic(
            this,
            &UColonistVisualManager::HandleTerrainReady
        );
        bBoundToTerrainReady = false;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredSpawnTimerHandle);
    }

    DestroyAllColonistActors();

    CachedProcessorManager = nullptr;
    CachedSimulationTimeManager = nullptr;
    CachedVoxelWorldManager = nullptr;
    PendingSpawnSettlementIDs.Reset();
    ActiveSettlementIDs.Reset();
    bSpawnDeferredScheduled = false;

    Super::Deinitialize();
}

void UColonistVisualManager::SpawnColonistActors(int32 InSettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    ActiveSettlementIDs.Add(InSettlementID);

    if (!CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    }

    if (!CachedVoxelWorldManager || !CachedVoxelWorldManager->IsSurfaceReady())
    {
        bSpawnDeferredScheduled = true;
        PendingSpawnSettlementIDs.Add(InSettlementID);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonistVisualManager] Waiting for terrain-ready signal before spawning colonists for settlement=%d."),
            InSettlementID
        );
        return;
    }

    bSpawnDeferredScheduled = false;
    InternalSpawnColonistActors(InSettlementID);
    PendingSpawnSettlementIDs.Remove(InSettlementID);
    bSpawnDeferredScheduled = PendingSpawnSettlementIDs.Num() > 0;
}

void UColonistVisualManager::HandleTerrainReady()
{
    if (!bSpawnDeferredScheduled || PendingSpawnSettlementIDs.Num() == 0)
    {
        return;
    }

    TArray<int32> SettlementsToSpawn = PendingSpawnSettlementIDs.Array();
    SettlementsToSpawn.Sort();

    PendingSpawnSettlementIDs.Reset();
    bSpawnDeferredScheduled = false;

    FString SettlementList;
    for (int32 i = 0; i < SettlementsToSpawn.Num(); ++i)
    {
        if (i > 0)
        {
            SettlementList += TEXT(",");
        }
        SettlementList += FString::FromInt(SettlementsToSpawn[i]);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Terrain-ready received. Spawning deferred settlements: %s"),
        *SettlementList
    );

    for (const int32 SettlementID : SettlementsToSpawn)
    {
        InternalSpawnColonistActors(SettlementID);
    }
}

void UColonistVisualManager::InternalSpawnColonistActors(int32 InSettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (!CachedProcessorManager)
    {
        CachedProcessorManager = World->GetSubsystem<UProcessorManager>();
    }

    if (!CachedProcessorManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Cannot spawn actors: ProcessorManager missing."));
        return;
    }

    ActiveSettlementIDs.Add(InSettlementID);

    const float CellSize = ResolveCellSize();
    const TArray<FAgentData>& Agents = CachedProcessorManager->GetAgentsForSettlement(InSettlementID);

    int32 SpawnedCount = 0;

    for (const FAgentData& AgentData : Agents)
    {
        if (AgentData.State == EAgentState::Dead)
        {
            continue;
        }

        if (ColonistActors.Contains(AgentData.AgentID))
        {
            continue;
        }

        if (SpawnActorFromData(AgentData, CellSize))
        {
            ++SpawnedCount;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] SpawnColonistActors settlement=%d spawned=%d totalTracked=%d"),
        InSettlementID,
        SpawnedCount,
        ColonistActors.Num()
    );
}

void UColonistVisualManager::SyncActorsToAgents(int32 InSettlementID)
{
    if (bIsSyncInProgress)
    {
        return;
    }

    if (PendingSpawnSettlementIDs.Contains(InSettlementID))
    {
        return;
    }

    bIsSyncInProgress = true;

    UWorld* World = GetWorld();
    if (!World)
    {
        bIsSyncInProgress = false;
        return;
    }

    if (!CachedProcessorManager)
    {
        CachedProcessorManager = World->GetSubsystem<UProcessorManager>();
    }

    if (!CachedProcessorManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Cannot sync actors: ProcessorManager missing."));
        bIsSyncInProgress = false;
        return;
    }

    ActiveSettlementIDs.Add(InSettlementID);

    const float CellSize = ResolveCellSize();
    const TArray<FAgentData>& Agents = CachedProcessorManager->GetAgentsForSettlement(InSettlementID);

    TSet<int32> LiveAgentIDs;
    LiveAgentIDs.Reserve(Agents.Num());

    for (const FAgentData& AgentData : Agents)
    {
        LiveAgentIDs.Add(AgentData.AgentID);

        if (AgentData.State == EAgentState::Dead)
        {
            DestroyActorForAgent(AgentData.AgentID);
            continue;
        }

        AWorldForgeColonist* ExistingActor = GetActorForAgent(AgentData.AgentID);

        if (!ExistingActor)
        {
            ExistingActor = SpawnActorFromData(AgentData, CellSize);
        }

        if (ExistingActor)
        {
            ExistingActor->UpdateVisuals(AgentData, 0.0f, CellSize);
        }
    }

    TArray<int32> TrackedAgentIDs;
    ColonistActors.GetKeys(TrackedAgentIDs);

    for (const int32 AgentID : TrackedAgentIDs)
    {
        if (!LiveAgentIDs.Contains(AgentID))
        {
            DestroyActorForAgent(AgentID);
        }
    }

    bIsSyncInProgress = false;
}

AWorldForgeColonist* UColonistVisualManager::GetActorForAgent(int32 AgentID) const
{
    if (const TObjectPtr<AWorldForgeColonist>* Found = ColonistActors.Find(AgentID))
    {
        return Found->Get();
    }

    return nullptr;
}

void UColonistVisualManager::DestroyAllColonistActors()
{
    TArray<int32> AgentIDs;
    ColonistActors.GetKeys(AgentIDs);

    for (const int32 AgentID : AgentIDs)
    {
        DestroyActorForAgent(AgentID);
    }

    ColonistActors.Empty();
}


bool UColonistVisualManager::LoadVisualRegistryAsset(UColonistVisualRegistryAsset* RegistryAsset)
{
    if (!RegistryAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] LoadVisualRegistryAsset failed: registry asset is null."));
        return false;
    }

    VisualRegistryAsset = RegistryAsset;
    RaceVisualTables = RegistryAsset->RaceVisualTables;

    if (!RegistryAsset->DefaultVisualRowName.IsNone())
    {
        DefaultVisualRowName = RegistryAsset->DefaultVisualRowName;
    }

    NativeFallbackVisual = RegistryAsset->NativeFallbackVisual;

    int32 ValidTableCount = 0;
    for (const FColonistRaceVisualTable& Entry : RaceVisualTables)
    {
        if (Entry.RaceTag.IsValid() && Entry.VisualTable)
        {
            ++ValidTableCount;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Loaded visual registry '%s'. Entries=%d ValidTables=%d DefaultRow=%s"),
        *GetNameSafe(RegistryAsset),
        RaceVisualTables.Num(),
        ValidTableCount,
        *DefaultVisualRowName.ToString());

    PreloadAllRaceVisualAssets();

    return true;
}

bool UColonistVisualManager::LoadDefaultVisualRegistryAsset()
{
    // Prefer the editable/default-configured path first, then try common project
    // locations. This keeps the system data-driven while still giving the MVP
    // a safe fallback if the registry asset is moved between folders.
    TArray<FSoftObjectPath> CandidatePaths;

    if (DefaultVisualRegistryPath.IsValid())
    {
        CandidatePaths.Add(DefaultVisualRegistryPath);
    }

    CandidatePaths.AddUnique(FSoftObjectPath(TEXT("/Game/WorldForge/Data/Races/DA_ColonistVisualRegistry.DA_ColonistVisualRegistry")));
    CandidatePaths.AddUnique(FSoftObjectPath(TEXT("/Game/WorldForge/Data/Colonists/DA_ColonistVisualRegistry.DA_ColonistVisualRegistry")));

    for (const FSoftObjectPath& CandidatePath : CandidatePaths)
    {
        if (!CandidatePath.IsValid())
        {
            continue;
        }

        UObject* LoadedObject = CandidatePath.TryLoad();
        UColonistVisualRegistryAsset* LoadedRegistry = Cast<UColonistVisualRegistryAsset>(LoadedObject);

        if (LoadedRegistry)
        {
            DefaultVisualRegistryPath = CandidatePath;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ColonistVisualManager] Found colonist visual registry at '%s'."),
                *CandidatePath.ToString());

            return LoadVisualRegistryAsset(LoadedRegistry);
        }
    }

    FString AttemptedPaths;
    for (const FSoftObjectPath& CandidatePath : CandidatePaths)
    {
        if (!AttemptedPaths.IsEmpty())
        {
            AttemptedPaths += TEXT(", ");
        }
        AttemptedPaths += CandidatePath.ToString();
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ColonistVisualManager] No colonist visual registry found. Attempted: %s. Using native fallback visuals until a registry is assigned or created."),
        *AttemptedPaths);

    return false;
}


void UColonistVisualManager::SetRaceVisualTable(FGameplayTag RaceTag, UDataTable* VisualTable)
{
    if (!RaceTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] SetRaceVisualTable ignored: RaceTag invalid."));
        return;
    }

    for (FColonistRaceVisualTable& Entry : RaceVisualTables)
    {
        if (Entry.RaceTag == RaceTag)
        {
            Entry.VisualTable = VisualTable;
            UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Updated visual table for race %s: %s"), *RaceTag.ToString(), *GetNameSafe(VisualTable));
            return;
        }
    }

    FColonistRaceVisualTable NewEntry;
    NewEntry.RaceTag = RaceTag;
    NewEntry.VisualTable = VisualTable;
    RaceVisualTables.Add(NewEntry);

    UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Registered visual table for race %s: %s"), *RaceTag.ToString(), *GetNameSafe(VisualTable));
}

UDataTable* UColonistVisualManager::GetRaceVisualTable(FGameplayTag RaceTag) const
{
    if (!RaceTag.IsValid())
    {
        return nullptr;
    }

    for (const FColonistRaceVisualTable& Entry : RaceVisualTables)
    {
        if (Entry.RaceTag == RaceTag)
        {
            return Entry.VisualTable;
        }
    }

    return nullptr;
}

TArray<FName> UColonistVisualManager::GetVisualRowNamesForRace(FGameplayTag RaceTag) const
{
    TArray<FName> RowNames;

    if (const UDataTable* Table = GetRaceVisualTable(RaceTag))
    {
        RowNames = Table->GetRowNames();
    }

    return RowNames;
}

bool UColonistVisualManager::GetVisualDefinitionForRace(
    FGameplayTag RaceTag,
    FName RowName,
    FWorldForgeColonistVisualRow& OutDefinition) const
{
    const UDataTable* Table = GetRaceVisualTable(RaceTag);
    if (!Table)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] No visual table registered for race %s."), *RaceTag.ToString());
        return false;
    }

    const TArray<FName> RowNames = Table->GetRowNames();
    if (RowNames.Num() <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Visual table %s for race %s has no rows."), *GetNameSafe(Table), *RaceTag.ToString());
        return false;
    }

    const FName SafeRowName = RowName.IsNone() ? DefaultVisualRowName : RowName;

    // 1) Exact requested/default registry row, for legacy tables that still use "Default".
    if (WF_FindVisualRow(Table, SafeRowName, OutDefinition, TEXT("ColonistVisualManagerExact")))
    {
        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Found exact visual row %s for race %s in table %s."), *SafeRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table));
        return true;
    }

    const FString RacePrefix = WF_RaceTagToVisualRowPrefix(RaceTag);

    // 2) Your current naming convention: Race_Male_Default, Race_Female_Default.
    const TArray<FName> PreferredRows =
    {
        FName(*FString::Printf(TEXT("%s_Male_Default"), *RacePrefix)),
        FName(*FString::Printf(TEXT("%s_Female_Default"), *RacePrefix)),
        FName(*FString::Printf(TEXT("%s_Default"), *RacePrefix))
    };

    for (const FName& PreferredRow : PreferredRows)
    {
        if (WF_FindVisualRow(Table, PreferredRow, OutDefinition, TEXT("ColonistVisualManagerPreferred")))
        {
            UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Auto-selected visual row %s for race %s in table %s."), *PreferredRow.ToString(), *RaceTag.ToString(), *GetNameSafe(Table));
            return true;
        }
    }

    FName MatchedRowName;

    // 3) Case/underscore-insensitive fallback: race + male + default.
    if (WF_FindFirstMatchingRow(Table, RowNames, { RacePrefix, TEXT("Male"), TEXT("Default") }, MatchedRowName, OutDefinition, TEXT("ColonistVisualManagerRaceMaleDefault")))
    {
        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Auto-matched visual row %s for race %s in table %s."), *MatchedRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table));
        return true;
    }

    // 4) Case/underscore-insensitive fallback: race + female + default.
    if (WF_FindFirstMatchingRow(Table, RowNames, { RacePrefix, TEXT("Female"), TEXT("Default") }, MatchedRowName, OutDefinition, TEXT("ColonistVisualManagerRaceFemaleDefault")))
    {
        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Auto-matched visual row %s for race %s in table %s."), *MatchedRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table));
        return true;
    }

    // 5) Any male default row in this race table.
    if (WF_FindFirstMatchingRow(Table, RowNames, { TEXT("Male"), TEXT("Default") }, MatchedRowName, OutDefinition, TEXT("ColonistVisualManagerAnyMaleDefault")))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Row %s missing for race %s in table %s. Using male default row %s."), *SafeRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table), *MatchedRowName.ToString());
        return true;
    }

    // 6) Any female default row in this race table.
    if (WF_FindFirstMatchingRow(Table, RowNames, { TEXT("Female"), TEXT("Default") }, MatchedRowName, OutDefinition, TEXT("ColonistVisualManagerAnyFemaleDefault")))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Row %s missing for race %s in table %s. Using female default row %s."), *SafeRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table), *MatchedRowName.ToString());
        return true;
    }

    // 7) Any row containing Default.
    if (WF_FindFirstMatchingRow(Table, RowNames, { TEXT("Default") }, MatchedRowName, OutDefinition, TEXT("ColonistVisualManagerAnyDefault")))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Row %s missing for race %s in table %s. Using default-like row %s."), *SafeRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table), *MatchedRowName.ToString());
        return true;
    }

    // 8) Last resort: first row in the DataTable.
    if (WF_FindVisualRow(Table, RowNames[0], OutDefinition, TEXT("ColonistVisualManagerFirstRowFallback")))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Row %s missing for race %s in table %s. Using first row %s."), *SafeRowName.ToString(), *RaceTag.ToString(), *GetNameSafe(Table), *RowNames[0].ToString());
        return true;
    }

    return false;
}

bool UColonistVisualManager::ResolveVisualDefinitionForAgent(
    const FAgentData& AgentData,
    FWorldForgeColonistVisualRow& OutDefinition) const
{
    if (GetVisualDefinitionForRace(AgentData.RaceTag, DefaultVisualRowName, OutDefinition))
    {
        return true;
    }

    OutDefinition = NativeFallbackVisual;
    OutDefinition.DisplayName = FText::FromString(TEXT("Native Fallback"));

    if (AgentData.RaceTag.ToString().Contains(TEXT("Dwarf")))
    {
        OutDefinition.DebugMeshScale = FVector(0.45f, 0.45f, 0.65f);
        OutDefinition.DebugColor = FLinearColor(0.85f, 0.55f, 0.25f, 1.0f);
    }
    else if (AgentData.RaceTag.ToString().Contains(TEXT("Elf")))
    {
        OutDefinition.DebugMeshScale = FVector(0.30f, 0.30f, 1.05f);
        OutDefinition.DebugColor = FLinearColor(0.35f, 0.85f, 0.45f, 1.0f);
    }
    else if (AgentData.RaceTag.ToString().Contains(TEXT("Human")))
    {
        OutDefinition.DebugMeshScale = FVector(0.35f, 0.35f, 0.85f);
        OutDefinition.DebugColor = FLinearColor(0.35f, 0.55f, 1.0f, 1.0f);
    }

    OutDefinition.bUseDebugMeshWhenNoAnimation = true;
    return false;
}


void UColonistVisualManager::PreloadSoftVisualAsset(const TSoftObjectPtr<UObject>& Asset, const FString& Context)
{
    if (Asset.IsNull())
    {
        return;
    }

    UObject* LoadedObject = Asset.Get();
    if (!LoadedObject)
    {
        LoadedObject = Asset.LoadSynchronous();
    }

    if (LoadedObject)
    {
        PreloadedVisualAssets.AddUnique(LoadedObject);
        UE_LOG(LogTemp, Verbose, TEXT("[ColonistVisualManager] Preloaded visual asset %s (%s)."), *GetNameSafe(LoadedObject), *Context);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Failed to preload visual asset '%s' (%s)."), *Asset.ToSoftObjectPath().ToString(), *Context);
    }
}

void UColonistVisualManager::PreloadVisualRowAssets(const FWorldForgeColonistVisualRow& Row, const FString& Context)
{
    PreloadSoftVisualAsset(Row.IdleVisualAsset, Context + TEXT(" Idle"));
    PreloadSoftVisualAsset(Row.WalkVisualAsset, Context + TEXT(" Walk"));
    PreloadSoftVisualAsset(Row.WorkVisualAsset, Context + TEXT(" Work"));
    PreloadSoftVisualAsset(Row.CarryVisualAsset, Context + TEXT(" Carry"));
    PreloadSoftVisualAsset(Row.FightVisualAsset, Context + TEXT(" Fight"));
    PreloadSoftVisualAsset(Row.HurtVisualAsset, Context + TEXT(" Hurt"));
    PreloadSoftVisualAsset(Row.RestVisualAsset, Context + TEXT(" Rest"));
    PreloadSoftVisualAsset(Row.SleepVisualAsset, Context + TEXT(" Sleep"));
    PreloadSoftVisualAsset(Row.DeadVisualAsset, Context + TEXT(" Dead"));
    PreloadSoftVisualAsset(Row.EatVisualAsset, Context + TEXT(" Eat"));
    PreloadSoftVisualAsset(Row.SocialVisualAsset, Context + TEXT(" Social"));
    PreloadSoftVisualAsset(Row.FleeVisualAsset, Context + TEXT(" Flee"));
    PreloadSoftVisualAsset(Row.IncapacitatedVisualAsset, Context + TEXT(" Incapacitated"));
    PreloadSoftVisualAsset(Row.MineVisualAsset, Context + TEXT(" Mine"));
    PreloadSoftVisualAsset(Row.FarmVisualAsset, Context + TEXT(" Farm"));
    PreloadSoftVisualAsset(Row.BuildVisualAsset, Context + TEXT(" Build"));
    PreloadSoftVisualAsset(Row.CraftVisualAsset, Context + TEXT(" Craft"));
    PreloadSoftVisualAsset(Row.ResearchVisualAsset, Context + TEXT(" Research"));
    PreloadSoftVisualAsset(Row.HaulVisualAsset, Context + TEXT(" Haul"));
    PreloadSoftVisualAsset(Row.DoctorVisualAsset, Context + TEXT(" Doctor"));
    PreloadSoftVisualAsset(Row.CookVisualAsset, Context + TEXT(" Cook"));
    PreloadSoftVisualAsset(Row.GuardVisualAsset, Context + TEXT(" Guard"));
    PreloadSoftVisualAsset(Row.TradeVisualAsset, Context + TEXT(" Trade"));
    PreloadSoftVisualAsset(Row.PrayVisualAsset, Context + TEXT(" Pray"));
    PreloadSoftVisualAsset(Row.PaperZDAnimationSource, Context + TEXT(" PaperZDSource"));
    PreloadSoftVisualAsset(Row.PaperZDAnimBP, Context + TEXT(" PaperZDAnimBP"));
}

int32 UColonistVisualManager::PreloadAllRaceVisualAssets()
{
    PreloadedVisualAssets.Reset();

    int32 ValidTables = 0;
    int32 RowCount = 0;

    for (const FColonistRaceVisualTable& Entry : RaceVisualTables)
    {
        if (!Entry.RaceTag.IsValid() || !Entry.VisualTable)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Skipping preload entry. RaceValid=%d Table=%s"), Entry.RaceTag.IsValid() ? 1 : 0, *GetNameSafe(Entry.VisualTable));
            continue;
        }

        ++ValidTables;
        const TArray<FName> RowNames = Entry.VisualTable->GetRowNames();

        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Preloading race visual table. Race=%s Table=%s Rows=%d"), *Entry.RaceTag.ToString(), *GetNameSafe(Entry.VisualTable), RowNames.Num());

        for (const FName& RowName : RowNames)
        {
            const FWorldForgeColonistVisualRow* Row = Entry.VisualTable->FindRow<FWorldForgeColonistVisualRow>(RowName, TEXT("ColonistVisualPreload"), false);
            if (!Row)
            {
                UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Failed to read visual row %s for race %s."), *RowName.ToString(), *Entry.RaceTag.ToString());
                continue;
            }

            ++RowCount;
            PreloadVisualRowAssets(*Row, FString::Printf(TEXT("Race=%s Row=%s"), *Entry.RaceTag.ToString(), *RowName.ToString()));
        }
    }

    PreloadedVisualAssetCount = PreloadedVisualAssets.Num();

    UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Preload complete. Tables=%d Rows=%d Assets=%d"), ValidTables, RowCount, PreloadedVisualAssetCount);
    return PreloadedVisualAssetCount;
}

void UColonistVisualManager::HandleSimulationStep(int32 StepNumber)
{
    if (ActiveSettlementIDs.Num() == 0)
    {
        return;
    }

    TArray<int32> SettlementsToSync = ActiveSettlementIDs.Array();
    SettlementsToSync.Sort();

    for (const int32 SettlementID : SettlementsToSync)
    {
        if (PendingSpawnSettlementIDs.Contains(SettlementID))
        {
            continue;
        }

        SyncActorsToAgents(SettlementID);
    }

    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[ColonistVisualManager] HandleSimulationStep step=%d activeSettlements=%d trackedActors=%d pendingSettlements=%d"),
        StepNumber,
        ActiveSettlementIDs.Num(),
        ColonistActors.Num(),
        PendingSpawnSettlementIDs.Num()
    );
}

AWorldForgeColonist* UColonistVisualManager::SpawnActorFromData(const FAgentData& AgentData, float CellSize)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (!CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    }

    UClass* ColonistClass = ColonistActorClass
        ? ColonistActorClass.Get()
        : AWorldForgeColonist::StaticClass();

    if (!ColonistClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Colonist class is invalid."));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AWorldForgeColonist* NewActor = World->SpawnActor<AWorldForgeColonist>(
        ColonistClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!NewActor)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ColonistVisualManager] Failed to spawn actor for AgentID=%d"),
            AgentData.AgentID
        );
        return nullptr;
    }

    NewActor->InitializeFromAgentData(AgentData, CellSize);

    FWorldForgeColonistVisualRow VisualDefinition;
    const bool bFoundRaceVisual = ResolveVisualDefinitionForAgent(AgentData, VisualDefinition);
    NewActor->ApplyVisualDefinition(VisualDefinition);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Applied visual definition for AgentID=%d Race=%s FoundRaceVisual=%d DisplayName='%s'"),
        AgentData.AgentID,
        *AgentData.RaceTag.ToString(),
        bFoundRaceVisual ? 1 : 0,
        *VisualDefinition.DisplayName.ToString());

    ColonistActors.Add(AgentData.AgentID, NewActor);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Spawned colonist visual for AgentID=%d SettlementID=%d Race=%s Class=%s"),
        AgentData.AgentID,
        AgentData.SettlementID,
        *AgentData.RaceTag.ToString(),
        *GetNameSafe(ColonistClass)
    );

    return NewActor;
}

void UColonistVisualManager::DestroyActorForAgent(int32 AgentID)
{
    TObjectPtr<AWorldForgeColonist>* Found = ColonistActors.Find(AgentID);
    if (!Found)
    {
        return;
    }

    if (AWorldForgeColonist* Actor = Found->Get())
    {
        Actor->Destroy();
    }

    ColonistActors.Remove(AgentID);

    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[ColonistVisualManager] Destroyed colonist visual for AgentID=%d"),
        AgentID
    );
}

float UColonistVisualManager::ResolveCellSize() const
{
    return 1.0f;
}

UColonistVisualManager* UWorldForgeColonistVisualBlueprintLibrary::GetColonistVisualManager(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }

    const UWorld* World = WorldContextObject->GetWorld();
    return World ? World->GetSubsystem<UColonistVisualManager>() : nullptr;
}

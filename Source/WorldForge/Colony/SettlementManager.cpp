#include "SettlementManager.h"
#include "GameplayTagContainer.h"
#include "WorldMapSubsystem.h"
#include "Engine/World.h"

void USettlementManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[SettlementManager] Initialized."));
}

int32 USettlementManager::CreateSettlement(const FString& Name, int32 WorldTileID)
{
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SettlementManager] CreateSettlement called. Existing settlement count=%d"),
        Settlements.Num()
    );

    // WorldForge now supports multiple active settlements: player colony plus
    // generated NPC civilization settlements. Do not collapse all creation
    // requests into settlement 0. Duplicate protection belongs at the caller
    // level when a specific WorldTileID/OwnerCivID needs uniqueness.

    FSettlementState NewSettlement;
    NewSettlement.SettlementID = NextSettlementID++;
    NewSettlement.Name = Name;
    NewSettlement.WorldTileID = WorldTileID;

    if (WorldTileID != INDEX_NONE)
    {
        NewSettlement.ClaimedWorldTileIDs.AddUnique(WorldTileID);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] Settlement %d initial claim added. WorldTileID=%d Claims=%d"),
            NewSettlement.SettlementID,
            WorldTileID,
            NewSettlement.ClaimedWorldTileIDs.Num()
        );
    }

    Settlements.Add(NewSettlement);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SettlementManager] Created '%s' ID=%d WorldTileID=%d Claims=%d"),
        *Name,
        NewSettlement.SettlementID,
        NewSettlement.WorldTileID,
        NewSettlement.ClaimedWorldTileIDs.Num()
    );

    return NewSettlement.SettlementID;
}

bool USettlementManager::SettlementExists(int32 ID) const
{
    return GetSettlement(ID) != nullptr;
}

bool USettlementManager::AddToStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount)
{
    FSettlementState* S = GetMutableSettlement(ID);
    if (!S || !Tag.IsValid() || Amount <= 0)
    {
        return false;
    }

    S->Stockpiles.FindOrAdd(Tag) += Amount;
    return true;
}

bool USettlementManager::ConsumeFromStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount)
{
    FSettlementState* S = GetMutableSettlement(ID);
    if (!S || !Tag.IsValid() || Amount <= 0)
    {
        return false;
    }

    int32* V = S->Stockpiles.Find(Tag);
    if (!V || *V < Amount)
    {
        return false;
    }

    *V -= Amount;
    if (*V <= 0)
    {
        S->Stockpiles.Remove(Tag);
    }

    return true;
}

int32 USettlementManager::GetStockpileAmount(int32 ID, const FGameplayTag& Tag) const
{
    const FSettlementState* S = GetSettlement(ID);
    if (!S || !Tag.IsValid())
    {
        return 0;
    }

    const int32* V = S->Stockpiles.Find(Tag);
    return V ? *V : 0;
}

bool USettlementManager::HasStockpile(int32 ID, const FGameplayTag& Tag, int32 Min) const
{
    return GetStockpileAmount(ID, Tag) >= Min;
}

void USettlementManager::AddBuilding(int32 ID, const FGameplayTag& Tag)
{
    FSettlementState* S = GetMutableSettlement(ID);
    if (S && Tag.IsValid() && !S->CompletedBuildings.Contains(Tag))
    {
        S->CompletedBuildings.Add(Tag);
        SetVisualDirty(ID);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] Building registered for settlement %d: %s"),
            ID,
            *Tag.ToString()
        );
    }
}

bool USettlementManager::HasBuilding(int32 ID, const FGameplayTag& Tag) const
{
    const FSettlementState* S = GetSettlement(ID);
    return S && Tag.IsValid() && S->CompletedBuildings.Contains(Tag);
}

TArray<FGameplayTag> USettlementManager::GetBuildings(int32 ID) const
{
    const FSettlementState* S = GetSettlement(ID);
    return S ? S->CompletedBuildings : TArray<FGameplayTag>();
}

FSettlementState USettlementManager::GetSettlementState(int32 ID) const
{
    const FSettlementState* S = GetSettlement(ID);
    return S ? *S : FSettlementState();
}

void USettlementManager::SetPlayerCurrency(int32 SettlementID, FGameplayTag CurrencyTag)
{
    FSettlementState* S = GetMutableSettlement(SettlementID);
    if (S)
    {
        S->PlayerCurrencyTag = CurrencyTag;
    }
}

bool USettlementManager::AddClaimedWorldTileToSettlement(int32 SettlementID, int32 WorldTileID)
{
    if (WorldTileID == INDEX_NONE)
    {
        return false;
    }

    FSettlementState* Settlement = GetMutableSettlement(SettlementID);
    if (!Settlement)
    {
        return false;
    }

    if (Settlement->ClaimedWorldTileIDs.Contains(WorldTileID))
    {
        return true;
    }

    Settlement->ClaimedWorldTileIDs.Add(WorldTileID);
    SetVisualDirty(SettlementID);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SettlementManager] Settlement %d claim added. WorldTileID=%d Claims=%d"),
        SettlementID,
        WorldTileID,
        Settlement->ClaimedWorldTileIDs.Num()
    );

    return true;
}

bool USettlementManager::DoesSettlementClaimWorldTile(int32 SettlementID, int32 WorldTileID) const
{
    if (WorldTileID == INDEX_NONE)
    {
        return false;
    }

    const FSettlementState* Settlement = GetSettlement(SettlementID);
    return Settlement && Settlement->ClaimedWorldTileIDs.Contains(WorldTileID);
}

const TArray<int32>* USettlementManager::GetClaimedWorldTilesForSettlement(int32 SettlementID) const
{
    const FSettlementState* Settlement = GetSettlement(SettlementID);
    return Settlement ? &Settlement->ClaimedWorldTileIDs : nullptr;
}

bool USettlementManager::CanPlayerClaimWorldTile(int32 WorldTileID, FString& OutReason) const
{
    OutReason.Empty();

    const FSettlementState* PlayerSettlement = GetSettlement(PlayerSettlementID);
    if (PlayerSettlementID == INDEX_NONE || !PlayerSettlement)
    {
        OutReason = TEXT("No player settlement exists.");
        return false;
    }

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    if (!WorldMapSubsystem || !WorldMapSubsystem->IsInitialized())
    {
        OutReason = TEXT("World map is unavailable.");
        return false;
    }

    int32 TileX = INDEX_NONE;
    int32 TileY = INDEX_NONE;
    if (!WorldMapSubsystem->TileIndexToXY(WorldTileID, TileX, TileY))
    {
        OutReason = TEXT("Invalid world tile.");
        return false;
    }

    FWorldTileData TileData;
    if (!WorldMapSubsystem->GetTileData(WorldTileID, TileData))
    {
        OutReason = TEXT("Invalid world tile.");
        return false;
    }

    if (PlayerSettlement->ClaimedWorldTileIDs.Contains(WorldTileID))
    {
        OutReason = TEXT("Tile is already claimed by your colony.");
        return false;
    }

    for (const FSettlementState& Settlement : Settlements)
    {
        if (Settlement.SettlementID == PlayerSettlementID)
        {
            continue;
        }

        if (Settlement.ClaimedWorldTileIDs.Contains(WorldTileID))
        {
            OutReason = TEXT("Tile is already claimed by another settlement.");
            return false;
        }
    }

    if (TileData.ControllingCivID >= 0)
    {
        OutReason = TEXT("Tile is controlled by another civilization.");
        return false;
    }

    if (!WorldMapSubsystem->IsTileLandLike(TileX, TileY))
    {
        // Reuses the existing conservative world-map land helper: invalid tiles,
        // water depth, ocean/sea/lake/river/underwater biomes, and low-elevation
        // sea cells are all rejected for player claim expansion.
        OutReason = TEXT("Tile is water or otherwise not claimable land.");
        return false;
    }

    if (!IsWorldTileAdjacentToPlayerClaim(WorldTileID))
    {
        OutReason = TEXT("Tile is not adjacent to existing claim.");
        return false;
    }

    OutReason = TEXT("Tile can be claimed.");
    return true;
}

bool USettlementManager::ClaimWorldTileForPlayerSettlement(int32 WorldTileID, FString& OutReason)
{
    if (!CanPlayerClaimWorldTile(WorldTileID, OutReason))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SettlementManager] Claim rejected. TileID=%d Reason=\"%s\""),
            WorldTileID,
            *OutReason);
        return false;
    }

    const int32 SettlementID = PlayerSettlementID;
    if (!AddClaimedWorldTileToSettlement(SettlementID, WorldTileID))
    {
        OutReason = TEXT("Failed to add tile claim.");
        UE_LOG(LogTemp, Warning,
            TEXT("[SettlementManager] Claim rejected. TileID=%d Reason=\"%s\""),
            WorldTileID,
            *OutReason);
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>())
        {
            WorldMapSubsystem->MarkMapTextureDirty();
            WorldMapSubsystem->MarkDetailedMapTextureDirty();
        }
    }

    const FSettlementState* PlayerSettlement = GetSettlement(SettlementID);
    const int32 ClaimCount = PlayerSettlement ? PlayerSettlement->ClaimedWorldTileIDs.Num() : 0;

    OutReason = TEXT("Tile claimed.");

    UE_LOG(LogTemp, Log,
        TEXT("[SettlementManager] Player claimed world tile. SettlementID=%d TileID=%d Claims=%d"),
        SettlementID,
        WorldTileID,
        ClaimCount);

    return true;
}

bool USettlementManager::IsWorldTileAdjacentToPlayerClaim(int32 WorldTileID) const
{
    const FSettlementState* PlayerSettlement = GetSettlement(PlayerSettlementID);
    if (PlayerSettlementID == INDEX_NONE || !PlayerSettlement)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    if (!WorldMapSubsystem || !WorldMapSubsystem->IsInitialized())
    {
        return false;
    }

    int32 CandidateX = INDEX_NONE;
    int32 CandidateY = INDEX_NONE;
    if (!WorldMapSubsystem->TileIndexToXY(WorldTileID, CandidateX, CandidateY))
    {
        return false;
    }

    for (const int32 ClaimedWorldTileID : PlayerSettlement->ClaimedWorldTileIDs)
    {
        int32 ClaimedX = INDEX_NONE;
        int32 ClaimedY = INDEX_NONE;
        if (!WorldMapSubsystem->TileIndexToXY(ClaimedWorldTileID, ClaimedX, ClaimedY))
        {
            continue;
        }

        const int32 DeltaX = FMath::Abs(CandidateX - ClaimedX);
        const int32 DeltaY = FMath::Abs(CandidateY - ClaimedY);
        if (DeltaX + DeltaY == 1)
        {
            return true;
        }
    }

    return false;
}

int32 USettlementManager::GetPlayerLandClaimCost(int32 WorldTileID) const
{
    // Patch 4G is backend-only. Economy/payment will be wired in a later patch;
    // no stockpile, gold, barter, influence, or other resources are consumed here.
    (void)WorldTileID;
    return 0;
}

bool USettlementManager::DoesPlayerSettlementClaimWorldTile(int32 WorldTileID) const
{
    const bool bClaimed =
        WorldTileID != INDEX_NONE &&
        PlayerSettlementID != INDEX_NONE &&
        DoesSettlementClaimWorldTile(PlayerSettlementID, WorldTileID);

    if (bLogClaimQueries)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] ClaimQuery WorldTileID=%d PlayerSettlement=%d Claimed=%s"),
            WorldTileID,
            PlayerSettlementID,
            bClaimed ? TEXT("true") : TEXT("false")
        );
    }

    return bClaimed;
}

bool USettlementManager::DoesPlayerSettlementClaimWorldPosition(const FVector& WorldPosition) const
{
    int32 WorldTileID = INDEX_NONE;

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    const bool bResolved = WorldMapSubsystem && WorldMapSubsystem->WorldPositionToWorldTileIndex(WorldPosition, WorldTileID);
    const bool bClaimed = bResolved && PlayerSettlementID != INDEX_NONE && DoesSettlementClaimWorldTile(PlayerSettlementID, WorldTileID);

    if (bLogClaimQueries)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] ClaimQuery WorldPosition=(%.1f,%.1f,%.1f) WorldTileID=%d PlayerSettlement=%d Claimed=%s"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            WorldTileID,
            PlayerSettlementID,
            bClaimed ? TEXT("true") : TEXT("false")
        );
    }

    return bClaimed;
}

bool USettlementManager::DoesPlayerSettlementClaimWorldCell(const FIntPoint& WorldCell) const
{
    int32 WorldTileID = INDEX_NONE;

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    const bool bResolved = WorldMapSubsystem && WorldMapSubsystem->WorldCellToWorldTileIndex(WorldCell, WorldTileID);
    const bool bClaimed = bResolved && PlayerSettlementID != INDEX_NONE && DoesSettlementClaimWorldTile(PlayerSettlementID, WorldTileID);

    if (bLogClaimQueries)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] ClaimQuery WorldCell=(%d,%d) WorldTileID=%d PlayerSettlement=%d Claimed=%s"),
            WorldCell.X,
            WorldCell.Y,
            WorldTileID,
            PlayerSettlementID,
            bClaimed ? TEXT("true") : TEXT("false")
        );
    }

    return bClaimed;
}

bool USettlementManager::DoesPlayerSettlementClaimVoxelCell(const FIntPoint& VoxelCell) const
{
    int32 WorldTileID = INDEX_NONE;

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    const bool bResolved = WorldMapSubsystem && WorldMapSubsystem->VoxelCellToWorldTileIndex(VoxelCell, WorldTileID);
    const bool bClaimed = bResolved && PlayerSettlementID != INDEX_NONE && DoesSettlementClaimWorldTile(PlayerSettlementID, WorldTileID);

    if (bLogClaimQueries)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SettlementManager] ClaimQuery VoxelCell=(%d,%d) WorldTileID=%d PlayerSettlement=%d Claimed=%s"),
            VoxelCell.X,
            VoxelCell.Y,
            WorldTileID,
            PlayerSettlementID,
            bClaimed ? TEXT("true") : TEXT("false")
        );
    }

    return bClaimed;
}

void USettlementManager::SetPlayerSettlementID(int32 SettlementID)
{
    if (!SettlementExists(SettlementID))
    {
        UE_LOG(LogTemp, Warning, TEXT("[SettlementManager] SetPlayerSettlementID ignored: settlement %d does not exist."), SettlementID);
        return;
    }

    PlayerSettlementID = SettlementID;

    UE_LOG(LogTemp, Log, TEXT("[SettlementManager] Player settlement set to ID=%d"), PlayerSettlementID);
}

void USettlementManager::ClearPlayerSettlementID()
{
    PlayerSettlementID = INDEX_NONE;
    UE_LOG(LogTemp, Log, TEXT("[SettlementManager] Player settlement cleared."));
}

void USettlementManager::SetVisualDirty(int32 SettlementID)
{
    if (SettlementID >= 0)
    {
        VisualDirtySettlements.Add(SettlementID);
    }
}

bool USettlementManager::IsVisualDirty(int32 SettlementID) const
{
    return VisualDirtySettlements.Contains(SettlementID);
}

void USettlementManager::ClearVisualDirty(int32 SettlementID)
{
    VisualDirtySettlements.Remove(SettlementID);
}

FSettlementState* USettlementManager::GetMutableSettlement(int32 ID)
{
    for (FSettlementState& S : Settlements)
    {
        if (S.SettlementID == ID)
        {
            return &S;
        }
    }

    return nullptr;
}

const FSettlementState* USettlementManager::GetSettlement(int32 ID) const
{
    for (const FSettlementState& S : Settlements)
    {
        if (S.SettlementID == ID)
        {
            return &S;
        }
    }

    return nullptr;
}

TArray<int32> USettlementManager::GetAllSettlementIDs() const
{
    TArray<int32> IDs;
    for (const FSettlementState& S : Settlements)
    {
        IDs.Add(S.SettlementID);
    }

    return IDs;
}

void USettlementManager::IncrementPopulation(int32 ID)
{
    if (FSettlementState* S = GetMutableSettlement(ID))
    {
        S->Population++;
        SetVisualDirty(ID);
    }
}

void USettlementManager::IncrementRacePopulation(int32 ID, FGameplayTag RaceTag)
{
    if (FSettlementState* S = GetMutableSettlement(ID))
    {
        if (RaceTag.IsValid())
        {
            S->RacePopulation.FindOrAdd(RaceTag)++;
        }
        SetVisualDirty(ID);
    }
}

void USettlementManager::DecrementRacePopulation(int32 ID, FGameplayTag RaceTag)
{
    if (FSettlementState* S = GetMutableSettlement(ID))
    {
        if (int32* Count = S->RacePopulation.Find(RaceTag))
        {
            *Count = FMath::Max(0, *Count - 1);
            if (*Count <= 0)
            {
                S->RacePopulation.Remove(RaceTag);
            }
        }
        SetVisualDirty(ID);
    }
}

TMap<FGameplayTag, int32> USettlementManager::GetRacePopulation(int32 ID) const
{
    const FSettlementState* S = GetSettlement(ID);
    return S ? S->RacePopulation : TMap<FGameplayTag, int32>();
}

void USettlementManager::DecrementPopulation(int32 ID)
{
    if (FSettlementState* S = GetMutableSettlement(ID))
    {
        S->Population = FMath::Max(0, S->Population - 1);
        SetVisualDirty(ID);
    }
}

int32 USettlementManager::GetPopulation(int32 ID) const
{
    const FSettlementState* S = GetSettlement(ID);
    return S ? S->Population : 0;
}

void USettlementManager::RestoreFromSave(const TArray<FSettlementState>& Saved)
{
    Settlements = Saved;
    NextSettlementID = 0;
    VisualDirtySettlements.Reset();

    for (const FSettlementState& S : Settlements)
    {
        NextSettlementID = FMath::Max(NextSettlementID, S.SettlementID + 1);
    }
}

// TEMP DEBUG HELPERS
// NOTE: These are temporary testing utilities for resource injection.
// Reminder: revert/remove before shipping or when proper debug tooling exists.
//
// Existing stockpile/resource flow remains unchanged above; these helpers are
// intentionally additive for testing so the normal sim/resource path is easy to restore.

void USettlementManager::Add10Stone(int32 SettlementID)
{
    static const FGameplayTag StoneTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Stone"));

    if (!AddToStockpile(SettlementID, StoneTag, 10))
    {
        UE_LOG(LogTemp, Warning, TEXT("[SettlementManager][Debug] Add10Stone failed for SettlementID=%d"), SettlementID);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[SettlementManager][Debug] Added 10 Stone to SettlementID=%d"), SettlementID);
}

void USettlementManager::Add100All(int32 SettlementID)
{
    static const FGameplayTag FoodTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Food"));
    static const FGameplayTag WoodTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Wood"));
    static const FGameplayTag StoneTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Stone"));

    bool bOk = true;
    bOk &= AddToStockpile(SettlementID, FoodTag, 100);
    bOk &= AddToStockpile(SettlementID, WoodTag, 100);
    bOk &= AddToStockpile(SettlementID, StoneTag, 100);

    if (!bOk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SettlementManager][Debug] Add100All failed for SettlementID=%d"), SettlementID);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[SettlementManager][Debug] Added 100 Food/Wood/Stone to SettlementID=%d"), SettlementID);
}

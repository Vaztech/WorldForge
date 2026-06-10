#include "SettlementManager.h"
#include "GameplayTagContainer.h"
#include "WorldMapSubsystem.h"
#include "Voxel/VoxelWorldManager.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace
{
    bool TryGetVoxelTerrainMode(const UVoxelWorldManager* VoxelWorldManager, ETerrainMode& OutTerrainMode)
    {
        if (!VoxelWorldManager)
        {
            return false;
        }

        // CurrentTerrainMode is a private reflected UPROPERTY; read it locally instead of adding a public API for one claim check.
        if (const FEnumProperty* TerrainModeProperty = FindFProperty<FEnumProperty>(VoxelWorldManager->GetClass(), TEXT("CurrentTerrainMode")))
        {
            const void* ValuePtr = TerrainModeProperty->ContainerPtrToValuePtr<void>(VoxelWorldManager);
            OutTerrainMode = static_cast<ETerrainMode>(TerrainModeProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr));
            return true;
        }

        if (const FByteProperty* TerrainModeByteProperty = FindFProperty<FByteProperty>(VoxelWorldManager->GetClass(), TEXT("CurrentTerrainMode")))
        {
            const void* ValuePtr = TerrainModeByteProperty->ContainerPtrToValuePtr<void>(VoxelWorldManager);
            OutTerrainMode = static_cast<ETerrainMode>(TerrainModeByteProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        return false;
    }

    const TCHAR* TerrainModeToString(ETerrainMode TerrainMode)
    {
        switch (TerrainMode)
        {
        case ETerrainMode::Smooth:
            return TEXT("Smooth");
        case ETerrainMode::Blocky:
            return TEXT("Blocky");
        case ETerrainMode::CubePrototype:
            return TEXT("CubePrototype");
        default:
            return TEXT("Unknown");
        }
    }
}

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

bool USettlementManager::AddStartingSeedInventory(int32 ID, FName CropRowName, int32 Amount)
{
    if (!SettlementExists(ID) || CropRowName.IsNone() || Amount <= 0)
    {
        return false;
    }

    FWorldForgeSettlementSeedInventory& Inventory = TemporarySeedInventories.FindOrAdd(ID);
    Inventory.CropSeedQuantities.FindOrAdd(CropRowName) += Amount;
    Inventory.CropSeedOrder.AddUnique(CropRowName);

    // Strip LP_ prefix for human-readable display name in log
    FString CropDisplay = CropRowName.ToString();
    if (CropDisplay.StartsWith(TEXT("LP_")))
    {
        CropDisplay.RightChopInline(3);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[SettlementManager] Seed inventory added. SettlementID=%d Crop=%s CropRow=%s Qty=%d"),
        ID,
        *CropDisplay,
        *CropRowName.ToString(),
        Amount);

    return true;
}

int32 USettlementManager::GetStartingSeedInventoryAmount(int32 ID, FName CropRowName) const
{
    const FWorldForgeSettlementSeedInventory* Inventory = TemporarySeedInventories.Find(ID);
    if (!Inventory || CropRowName.IsNone())
    {
        return 0;
    }

    const int32* Quantity = Inventory->CropSeedQuantities.Find(CropRowName);
    return Quantity ? FMath::Max(0, *Quantity) : 0;
}

TMap<FName, int32> USettlementManager::GetStartingSeedInventory(int32 ID) const
{
    const FWorldForgeSettlementSeedInventory* Inventory = TemporarySeedInventories.Find(ID);
    return Inventory ? Inventory->CropSeedQuantities : TMap<FName, int32>();
}

TArray<FName> USettlementManager::GetStartingSeedInventoryOrder(int32 ID) const
{
    const FWorldForgeSettlementSeedInventory* Inventory = TemporarySeedInventories.Find(ID);
    return Inventory ? Inventory->CropSeedOrder : TArray<FName>();
}

bool USettlementManager::ConsumeStartingSeedInventory(int32 SettlementID, FName CropRowName, int32 Amount)
{
    if (!SettlementExists(SettlementID) || CropRowName.IsNone() || Amount <= 0)
    {
        return false;
    }

    FWorldForgeSettlementSeedInventory* Inventory = TemporarySeedInventories.Find(SettlementID);
    if (!Inventory)
    {
        return false;
    }

    int32* QuantityPtr = Inventory->CropSeedQuantities.Find(CropRowName);
    if (!QuantityPtr || *QuantityPtr < Amount)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SettlementManager] ConsumeStartingSeedInventory failed: SettlementID=%d CropRow=%s Requested=%d Available=%d"),
            SettlementID,
            *CropRowName.ToString(),
            Amount,
            QuantityPtr ? *QuantityPtr : 0);
        return false;
    }

    const int32 QuantityBefore = *QuantityPtr;
    *QuantityPtr = FMath::Max(0, *QuantityPtr - Amount);

    UE_LOG(LogTemp, Log,
        TEXT("[SettlementManager] ConsumeStartingSeedInventory: SettlementID=%d CropRow=%s Before=%d Consumed=%d After=%d"),
        SettlementID,
        *CropRowName.ToString(),
        QuantityBefore,
        Amount,
        *QuantityPtr);

    return true;
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

bool USettlementManager::DoesSettlementClaimVoxelCell(int32 SettlementID, const FIntPoint& VoxelCell) const
{
    int32 WorldTileID = INDEX_NONE;
    FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
    int32 ClaimedWorldTileCount = 0;
    return DoesSettlementClaimVoxelCellDetailed(
        SettlementID,
        VoxelCell,
        WorldTileID,
        WorldTileXY,
        ClaimedWorldTileCount);
}

bool USettlementManager::DoesSettlementClaimVoxelCellDetailed(
    int32 SettlementID,
    const FIntPoint& VoxelCell,
    int32& OutWorldTileID,
    FIntPoint& OutWorldTileXY,
    int32& OutClaimedWorldTileCount) const
{
    OutWorldTileID = INDEX_NONE;
    OutWorldTileXY = FIntPoint(INDEX_NONE, INDEX_NONE);
    OutClaimedWorldTileCount = 0;

    const FSettlementState* Settlement = GetSettlement(SettlementID);
    if (!Settlement)
    {
        if (bLogClaimQueries)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SettlementManager] ClaimQuery VoxelCell=(%d,%d) SettlementID=%d WorldTile=(%d,%d) WorldTileID=%d Claims=%d Claimed=false Reason=no settlement"),
                VoxelCell.X,
                VoxelCell.Y,
                SettlementID,
                OutWorldTileXY.X,
                OutWorldTileXY.Y,
                OutWorldTileID,
                OutClaimedWorldTileCount);
        }
        return false;
    }

    OutClaimedWorldTileCount = Settlement->ClaimedWorldTileIDs.Num();

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    const UVoxelWorldManager* VoxelWorldManager = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;

    ETerrainMode TerrainMode = ETerrainMode::Smooth;
    const bool bTerrainModeResolved = TryGetVoxelTerrainMode(VoxelWorldManager, TerrainMode);

    // After CUBE-MINING-V2, callers may pass absolute world-tile XY directly
    // (e.g. MineCellTarget) rather than voxel-cell coordinates. Try the
    // direct interpretation first; fall back to the legacy voxel->chunk
    // conversion only if that does not resolve to a claimed tile.
    const FIntPoint DirectCell = VoxelCell;
    int32 DirectTileID = INDEX_NONE;
    const bool bDirectResolved = WorldMapSubsystem && WorldMapSubsystem->WorldCellToWorldTileIndex(DirectCell, DirectTileID);
    const bool bDirectClaimed = bDirectResolved && Settlement->ClaimedWorldTileIDs.Contains(DirectTileID);

    const FIntPoint ConvertedCell = VoxelWorldManager
        ? VoxelWorldManager->VoxelCellToChunkCell(VoxelCell)
        : VoxelCell;
    int32 ConvertedTileID = INDEX_NONE;
    const bool bConvertedResolved = WorldMapSubsystem && WorldMapSubsystem->WorldCellToWorldTileIndex(ConvertedCell, ConvertedTileID);
    const bool bConvertedClaimed = bConvertedResolved && Settlement->ClaimedWorldTileIDs.Contains(ConvertedTileID);

    FIntPoint FinalCell = DirectCell;
    bool bResolved = false;
    bool bClaimed = false;
    if (bDirectClaimed)
    {
        FinalCell = DirectCell;
        OutWorldTileID = DirectTileID;
        bResolved = true;
        bClaimed = true;
    }
    else if (bConvertedClaimed)
    {
        FinalCell = ConvertedCell;
        OutWorldTileID = ConvertedTileID;
        bResolved = true;
        bClaimed = true;
    }
    else
    {
        // Preserve failure path. Prefer the converted interpretation when it
        // at least resolved to a real tile; otherwise fall back to the direct
        // interpretation so OutWorldTileID/OutWorldTileXY still reflect the
        // best available resolution for downstream diagnostics.
        if (bConvertedResolved)
        {
            FinalCell = ConvertedCell;
            OutWorldTileID = ConvertedTileID;
            bResolved = true;
        }
        else if (bDirectResolved)
        {
            FinalCell = DirectCell;
            OutWorldTileID = DirectTileID;
            bResolved = true;
        }
        else
        {
            FinalCell = DirectCell;
            OutWorldTileID = INDEX_NONE;
            bResolved = false;
        }
    }

    if (bResolved && WorldMapSubsystem)
    {
        int32 TileX = INDEX_NONE;
        int32 TileY = INDEX_NONE;
        if (WorldMapSubsystem->TileIndexToXY(OutWorldTileID, TileX, TileY))
        {
            OutWorldTileXY = FIntPoint(TileX, TileY);
        }
    }

    const TCHAR* TerrainModeText = bTerrainModeResolved ? TerrainModeToString(TerrainMode) : TEXT("Unknown");

    UE_LOG(LogTemp, Log,
        TEXT("[CubeSelectionClaimAudit] ClaimConversion TerrainMode=%s InputCell=(%d,%d) DirectCell=(%d,%d) DirectTileID=%d DirectClaimed=%s ConvertedCell=(%d,%d) ConvertedTileID=%d ConvertedClaimed=%s FinalCell=(%d,%d) Claimed=%s"),
        TerrainModeText,
        VoxelCell.X,
        VoxelCell.Y,
        DirectCell.X,
        DirectCell.Y,
        DirectTileID,
        bDirectClaimed ? TEXT("true") : TEXT("false"),
        ConvertedCell.X,
        ConvertedCell.Y,
        ConvertedTileID,
        bConvertedClaimed ? TEXT("true") : TEXT("false"),
        FinalCell.X,
        FinalCell.Y,
        bClaimed ? TEXT("true") : TEXT("false"));

    if (bLogClaimQueries)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[SettlementManager] ClaimQuery VoxelCell=(%d,%d) SettlementID=%d WorldTile=(%d,%d) WorldTileID=%d Claims=%d Claimed=%s Resolved=%s"),
            VoxelCell.X,
            VoxelCell.Y,
            SettlementID,
            OutWorldTileXY.X,
            OutWorldTileXY.Y,
            OutWorldTileID,
            OutClaimedWorldTileCount,
            bClaimed ? TEXT("true") : TEXT("false"),
            bResolved ? TEXT("true") : TEXT("false"));
    }

    return bClaimed;
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

bool USettlementManager::AddInitialPlayerClaimFootprint(int32 SettlementID, int32 CenterTileX, int32 CenterTileY, int32 Radius)
{
    const int32 EffectiveRadius = FMath::Max(0, Radius);
    const int32 FootprintDiameter = (EffectiveRadius * 2) + 1;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SettlementManager] Initial player claim footprint center=(%d,%d) radius=%d footprint=%dx%d"),
        CenterTileX,
        CenterTileY,
        EffectiveRadius,
        FootprintDiameter,
        FootprintDiameter
    );

    FSettlementState* Settlement = GetMutableSettlement(SettlementID);
    if (!Settlement)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SettlementManager] Initial player claim complete. SettlementID=%d Claims=0"),
            SettlementID);
        return false;
    }

    const UWorld* World = GetWorld();
    const UWorldMapSubsystem* WorldMapSubsystem = World ? World->GetSubsystem<UWorldMapSubsystem>() : nullptr;
    const bool bWorldMapAvailable = WorldMapSubsystem && WorldMapSubsystem->IsInitialized();

    bool bAddedAny = false;
    int32 AddedCount = 0;
    int32 SkippedInvalidCount = 0;
    int32 SkippedAlreadyClaimedCount = 0;
    int32 SkippedControlledCount = 0;
    int32 SkippedWaterCount = 0;

    for (int32 OffsetY = -EffectiveRadius; OffsetY <= EffectiveRadius; ++OffsetY)
    {
        for (int32 OffsetX = -EffectiveRadius; OffsetX <= EffectiveRadius; ++OffsetX)
        {
            const int32 TileX = CenterTileX + OffsetX;
            const int32 TileY = CenterTileY + OffsetY;

            int32 CandidateTileID = INDEX_NONE;
            if (!bWorldMapAvailable || !WorldMapSubsystem->TileXYToIndex(TileX, TileY, CandidateTileID))
            {
                ++SkippedInvalidCount;
                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim skipped. Tile=(%d,%d) Reason=invalid"),
                    TileX,
                    TileY);
                continue;
            }

            if (Settlement->ClaimedWorldTileIDs.Contains(CandidateTileID))
            {
                ++SkippedAlreadyClaimedCount;
                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim skipped. Tile=(%d,%d) Reason=already claimed"),
                    TileX,
                    TileY);
                continue;
            }

            bool bClaimedByOtherSettlement = false;
            for (const FSettlementState& ExistingSettlement : Settlements)
            {
                if (ExistingSettlement.SettlementID == SettlementID)
                {
                    continue;
                }

                if (ExistingSettlement.ClaimedWorldTileIDs.Contains(CandidateTileID))
                {
                    bClaimedByOtherSettlement = true;
                    break;
                }
            }

            if (bClaimedByOtherSettlement || WorldMapSubsystem->IsTileClaimed(TileX, TileY))
            {
                ++SkippedControlledCount;
                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim skipped. Tile=(%d,%d) Reason=controlled"),
                    TileX,
                    TileY);
                continue;
            }

            if (!WorldMapSubsystem->IsTileLandLike(TileX, TileY))
            {
                ++SkippedWaterCount;
                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim skipped. Tile=(%d,%d) Reason=water"),
                    TileX,
                    TileY);
                continue;
            }

            if (AddClaimedWorldTileToSettlement(SettlementID, CandidateTileID))
            {
                bAddedAny = true;
                ++AddedCount;

                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim added. Tile=(%d,%d) TileID=%d"),
                    TileX,
                    TileY,
                    CandidateTileID);
            }
            else
            {
                ++SkippedInvalidCount;
                UE_LOG(LogTemp, Log,
                    TEXT("[SettlementManager] Initial claim skipped. Tile=(%d,%d) Reason=invalid"),
                    TileX,
                    TileY);
            }
        }
    }

    const FSettlementState* UpdatedSettlement = GetSettlement(SettlementID);
    const int32 ClaimCount = UpdatedSettlement ? UpdatedSettlement->ClaimedWorldTileIDs.Num() : 0;

    UE_LOG(LogTemp, Log,
        TEXT("[SettlementManager] Initial player claim complete. SettlementID=%d Center=(%d,%d) Radius=%d Bounds=(%d,%d)->(%d,%d) Claims=%d Added=%d SkippedInvalid=%d SkippedAlreadyClaimed=%d SkippedControlled=%d SkippedWater=%d"),
        SettlementID,
        CenterTileX,
        CenterTileY,
        EffectiveRadius,
        CenterTileX - EffectiveRadius,
        CenterTileY - EffectiveRadius,
        CenterTileX + EffectiveRadius,
        CenterTileY + EffectiveRadius,
        ClaimCount,
        AddedCount,
        SkippedInvalidCount,
        SkippedAlreadyClaimedCount,
        SkippedControlledCount,
        SkippedWaterCount);

    const int32 FailedCount = SkippedInvalidCount + SkippedAlreadyClaimedCount + SkippedControlledCount + SkippedWaterCount;
    UE_LOG(LogTemp, Log,
        TEXT("[CubeSelectionClaimAudit] AddInitialClaim SettlementID=%d Center=(%d,%d) Radius=%d Added=%d Failed=%d"),
        SettlementID,
        CenterTileX,
        CenterTileY,
        EffectiveRadius,
        AddedCount,
        FailedCount);

    return bAddedAny;
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
    FIntPoint WorldTileXY(INDEX_NONE, INDEX_NONE);
    int32 PlayerSettlement = INDEX_NONE;
    int32 ClaimedWorldTileCount = 0;
    return DoesPlayerSettlementClaimVoxelCellDetailed(
        VoxelCell,
        WorldTileID,
        WorldTileXY,
        PlayerSettlement,
        ClaimedWorldTileCount);
}

bool USettlementManager::DoesPlayerSettlementClaimVoxelCellDetailed(
    const FIntPoint& VoxelCell,
    int32& OutWorldTileID,
    FIntPoint& OutWorldTileXY,
    int32& OutPlayerSettlementID,
    int32& OutClaimedWorldTileCount) const
{
    OutPlayerSettlementID = PlayerSettlementID;

    if (PlayerSettlementID == INDEX_NONE)
    {
        OutWorldTileID = INDEX_NONE;
        OutWorldTileXY = FIntPoint(INDEX_NONE, INDEX_NONE);
        OutClaimedWorldTileCount = 0;

        if (bLogClaimQueries)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SettlementManager] ClaimQuery VoxelCell=(%d,%d) PlayerSettlement=%d WorldTile=(%d,%d) WorldTileID=%d Claims=%d Claimed=false Reason=no player settlement"),
                VoxelCell.X,
                VoxelCell.Y,
                OutPlayerSettlementID,
                OutWorldTileXY.X,
                OutWorldTileXY.Y,
                OutWorldTileID,
                OutClaimedWorldTileCount);
        }

        return false;
    }

    return DoesSettlementClaimVoxelCellDetailed(
        PlayerSettlementID,
        VoxelCell,
        OutWorldTileID,
        OutWorldTileXY,
        OutClaimedWorldTileCount);
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

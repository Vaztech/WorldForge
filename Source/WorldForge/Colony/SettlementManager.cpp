#include "SettlementManager.h"
#include "GameplayTagContainer.h"

void USettlementManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[SettlementManager] Initialized."));
}

int32 USettlementManager::CreateSettlement(const FString& Name, int32 TileID)
{
    FSettlementState S;
    S.SettlementID = NextSettlementID++;
    S.Name = Name;
    S.WorldTileID = TileID;
    S.Population = 0;
    S.MoraleSummary = 50.f;

    Settlements.Add(S);

    UE_LOG(LogTemp, Log, TEXT("[SettlementManager] Created '%s' ID=%d"), *Name, S.SettlementID);
    return S.SettlementID;
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
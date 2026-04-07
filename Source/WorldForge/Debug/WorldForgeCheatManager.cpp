#include "WorldForgeCheatManager.h"
#include "Engine/World.h"
#include "SettlementManager.h"
#include "GameplayTagContainer.h"

void UWorldForgeCheatManager::Add10Stone(int32 SettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] Add10Stone failed: no world"));
        return;
    }

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (!SM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] Add10Stone failed: SettlementManager missing"));
        return;
    }

    SM->Add10Stone(SettlementID);
}

void UWorldForgeCheatManager::Add100All(int32 SettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] Add100All failed: no world"));
        return;
    }

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (!SM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] Add100All failed: SettlementManager missing"));
        return;
    }

    SM->Add100All(SettlementID);
}

void UWorldForgeCheatManager::PrintStockpile(int32 SettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] PrintStockpile failed: no world"));
        return;
    }

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (!SM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] PrintStockpile failed: SettlementManager missing"));
        return;
    }

    const FGameplayTag FoodTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Food"));
    const FGameplayTag WoodTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Wood"));
    const FGameplayTag StoneTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Stone"));

    const int32 Food = SM->GetStockpileAmount(SettlementID, FoodTag);
    const int32 Wood = SM->GetStockpileAmount(SettlementID, WoodTag);
    const int32 Stone = SM->GetStockpileAmount(SettlementID, StoneTag);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[CheatManager] Settlement %d Stockpile => Food=%d Wood=%d Stone=%d"),
        SettlementID,
        Food,
        Wood,
        Stone
    );
}

void UWorldForgeCheatManager::AddResource(const FString& Resource, int32 Amount, int32 SettlementID)
{
    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] AddResource failed: Amount must be > 0"));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] AddResource failed: no world"));
        return;
    }

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (!SM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CheatManager] AddResource failed: SettlementManager missing"));
        return;
    }

    FString Normalized = Resource;
    Normalized.TrimStartAndEndInline();
    Normalized = Normalized.ToLower();

    FGameplayTag ResourceTag;

    if (Normalized == TEXT("food"))
    {
        ResourceTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Food"));
    }
    else if (Normalized == TEXT("wood"))
    {
        ResourceTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Wood"));
    }
    else if (Normalized == TEXT("stone"))
    {
        ResourceTag = FGameplayTag::RequestGameplayTag(TEXT("WorldForge.Item.Stone"));
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[CheatManager] AddResource failed: unknown resource '%s' (valid: Food, Wood, Stone)"),
            *Resource
        );
        return;
    }

    if (!SM->AddToStockpile(SettlementID, ResourceTag, Amount))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[CheatManager] AddResource failed: could not add %d of %s to SettlementID=%d"),
            Amount,
            *Resource,
            SettlementID
        );
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[CheatManager] Added %d %s to SettlementID=%d"),
        Amount,
        *Resource,
        SettlementID
    );
}
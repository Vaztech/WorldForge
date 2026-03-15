#include "WorldForgeDataRegistry.h"
#include "Logging/LogMacros.h"

void UWorldForgeDataRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("[WorldForgeDataRegistry] Initialized. Ready for content registration."));
    // Content is registered by GameMode or AssetManager in a later phase.
    // During Phase 0 the registry starts empty - this is correct.
}

void UWorldForgeDataRegistry::RegisterItem(UItemDefinition* Def)
{
    if (!Def)
    {
        return;
    }

    if (!Def->ItemTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldForgeDataRegistry] Attempted to register item with invalid tag."));
        return;
    }

    Items.Add(Def->ItemTag, Def);
    UE_LOG(LogTemp, Verbose, TEXT("[WorldForgeDataRegistry] Item registered: %s"), *Def->ItemTag.ToString());
}

void UWorldForgeDataRegistry::RegisterBuilding(UBuildingDefinition* Def)
{
    if (!Def)
    {
        return;
    }

    if (!Def->BuildingTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldForgeDataRegistry] Attempted to register building with invalid tag."));
        return;
    }

    Buildings.Add(Def->BuildingTag, Def);
    UE_LOG(LogTemp, Verbose, TEXT("[WorldForgeDataRegistry] Building registered: %s"), *Def->BuildingTag.ToString());
}

void UWorldForgeDataRegistry::RegisterJob(UJobDefinition* Def)
{
    if (!Def)
    {
        return;
    }

    if (!Def->JobTag.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldForgeDataRegistry] Attempted to register job with invalid tag."));
        return;
    }

    Jobs.Add(Def->JobTag, Def);
    UE_LOG(LogTemp, Verbose, TEXT("[WorldForgeDataRegistry] Job registered: %s"), *Def->JobTag.ToString());
}

const UItemDefinition* UWorldForgeDataRegistry::FindItem(FGameplayTag Tag) const
{
    const UItemDefinition* const* Found = Items.Find(Tag);
    return Found ? *Found : nullptr;
}

const UBuildingDefinition* UWorldForgeDataRegistry::FindBuilding(FGameplayTag Tag) const
{
    const UBuildingDefinition* const* Found = Buildings.Find(Tag);
    return Found ? *Found : nullptr;
}

const UJobDefinition* UWorldForgeDataRegistry::FindJob(FGameplayTag Tag) const
{
    const UJobDefinition* const* Found = Jobs.Find(Tag);
    return Found ? *Found : nullptr;
}

void UWorldForgeDataRegistry::RegisterModContent(const FString& ModID, UPrimaryDataAsset* Asset)
{
    if (!Asset)
    {
        return;
    }

    if (UItemDefinition* Item = Cast<UItemDefinition>(Asset))
    {
        RegisterItem(Item);
        return;
    }

    if (UBuildingDefinition* Building = Cast<UBuildingDefinition>(Asset))
    {
        RegisterBuilding(Building);
        return;
    }

    if (UJobDefinition* Job = Cast<UJobDefinition>(Asset))
    {
        RegisterJob(Job);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[WorldForgeDataRegistry] Mod %s: unrecognized asset type."), *ModID);
}
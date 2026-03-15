#include "ContentFilterSubsystem.h"
#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

void UContentFilterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadFromConfig();

    UE_LOG(LogTemp, Log, TEXT("[ContentFilter] Initialized. All 9 filters default: Full."));
}

bool UContentFilterSubsystem::IsEventAllowed(FGameplayTag EventCategoryTag) const
{
    const FString Tag = EventCategoryTag.ToString();

    auto Check = [](EFilterLevel Level)
        {
            return Level != EFilterLevel::Disabled;
        };

    if (Tag.Contains(TEXT("Slavery")) && !Check(Slavery)) return false;
    if (Tag.Contains(TEXT("Violence")) && !Check(GraphicViolence)) return false;
    if (Tag.Contains(TEXT("Substance")) && !Check(SubstanceAbuse)) return false;
    if (Tag.Contains(TEXT("Mental")) && !Check(MentalHealth)) return false;
    if (Tag.Contains(TEXT("Torture")) && !Check(Torture)) return false;
    if (Tag.Contains(TEXT("Religious")) && !Check(ReligiousExtreme)) return false;
    if (Tag.Contains(TEXT("Divine")) && !Check(DivineCoercion)) return false;
    if (Tag.Contains(TEXT("Cannibalism")) && !Check(Cannibalism)) return false;
    if (Tag.Contains(TEXT("Economic")) && !Check(EconomicExploitation)) return false;

    return true;
}

void UContentFilterSubsystem::LoadFromConfig()
{
    // TODO Phase 2: Load filter settings from saved config file.
    // All filters default to Full until player changes them in settings.
}

void UContentFilterSubsystem::SaveToConfig() const
{
    // TODO Phase 2: Save filter settings to config file.
}
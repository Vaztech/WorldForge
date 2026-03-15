#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "ContentFilterSubsystem.generated.h"

UENUM(BlueprintType)
enum class EFilterLevel : uint8
{
    Full        UMETA(DisplayName = "Full Simulation"),
    Simplified  UMETA(DisplayName = "Simplified"),
    Disabled    UMETA(DisplayName = "Disabled")
};

UCLASS()
class WORLDFORGE_API UContentFilterSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // ── 9 Filter Categories — all default to Full ─────────────────
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel Slavery = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel GraphicViolence = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel SubstanceAbuse = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel MentalHealth = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel Torture = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel ReligiousExtreme = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel DivineCoercion = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel Cannibalism = EFilterLevel::Full;
    UPROPERTY(BlueprintReadWrite, Category = "Content Filters") EFilterLevel EconomicExploitation = EFilterLevel::Full;

    // Call this BEFORE firing any dark event.
    // Returns false = event should NOT fire. Returns true = event may fire.
    UFUNCTION(BlueprintCallable, Category = "Content Filters")
    bool IsEventAllowed(FGameplayTag EventCategoryTag) const;

    // Load/save settings — implemented in Phase 2
    void LoadFromConfig();
    void SaveToConfig() const;
};
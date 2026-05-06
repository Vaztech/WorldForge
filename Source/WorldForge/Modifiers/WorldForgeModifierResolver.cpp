#include "Modifiers/WorldForgeModifierResolver.h"

#include "Engine/DataTable.h"
#include "Traits/WorldForgeTraitLibrary.h"
#include "Traits/WorldForgeTraitTypes.h"
#include "Modifiers/WorldForgeEquipmentTypes.h"

// ====================================================================
// DATA TABLE LOADERS
// ====================================================================

UDataTable* UWorldForgeModifierResolver::GetTraitTable()
{
    static UDataTable* CachedTraitTable = nullptr;
    static bool bAttemptedLoad = false;

    if (!bAttemptedLoad)
    {
        bAttemptedLoad = true;

        CachedTraitTable = LoadObject<UDataTable>(
            nullptr,
            TEXT("/Game/Data/Traits/DT_WorldForgeTraits.DT_WorldForgeTraits"));

        if (CachedTraitTable)
        {
            UE_LOG(LogTemp, Log, TEXT("[ModifierResolver] Loaded DT_WorldForgeTraits."));
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ModifierResolver] Could not load DT_WorldForgeTraits at /Game/Data/Traits/DT_WorldForgeTraits."));
        }
    }

    return CachedTraitTable;
}


    UDataTable* UWorldForgeModifierResolver::GetEquipmentTable()
    {
        static UDataTable* CachedEquipmentTable = nullptr;
        static bool bAttemptedLoad = false;

        if (!bAttemptedLoad)
        {
            bAttemptedLoad = true;

            CachedEquipmentTable = LoadObject<UDataTable>(
                nullptr,
                TEXT("/Game/Data/Equipment/DT_WorldForgeEquipment.DT_WorldForgeEquipment"));

            if (CachedEquipmentTable)
            {
                UE_LOG(LogTemp, Log, TEXT("[ModifierResolver] Loaded DT_WorldForgeEquipment."));
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ModifierResolver] Could not load DT_WorldForgeEquipment at /Game/Data/Equipment/DT_WorldForgeEquipment."));
            }
        }

        return CachedEquipmentTable;
    }

    namespace
    {

    void ApplyResolvedModifiers(
        FWorldForgeResolvedModifiers& Result,
        const FWorldForgeResolvedModifiers& Incoming)
    {
        Result.WorkEfficiencyMultiplier *= Incoming.WorkEfficiencyMultiplier;
        Result.HealingMultiplier *= Incoming.HealingMultiplier;
        Result.InjuryChanceMultiplier *= Incoming.InjuryChanceMultiplier;
        Result.ImmunityMultiplier *= Incoming.ImmunityMultiplier;

        Result.MoodModifier += Incoming.MoodModifier;
        Result.MoraleDeltaPerStep += Incoming.MoraleDeltaPerStep;
        Result.SocialModifier += Incoming.SocialModifier;
        Result.LikeabilityModifier += Incoming.LikeabilityModifier;
        Result.AnnoyanceModifier += Incoming.AnnoyanceModifier;

        for (const TPair<FGameplayTag, float>& SkillMod : Incoming.SkillModifiers)
        {
            if (!SkillMod.Key.IsValid())
            {
                continue;
            }

            float& ExistingValue = Result.SkillModifiers.FindOrAdd(SkillMod.Key);
            ExistingValue += SkillMod.Value;
        }
    }

    void ClampResolvedMultipliers(FWorldForgeResolvedModifiers& Result)
    {
        Result.WorkEfficiencyMultiplier = FMath::Max(0.0f, Result.WorkEfficiencyMultiplier);
        Result.HealingMultiplier = FMath::Max(0.0f, Result.HealingMultiplier);
        Result.InjuryChanceMultiplier = FMath::Max(0.0f, Result.InjuryChanceMultiplier);
        Result.ImmunityMultiplier = FMath::Max(0.0f, Result.ImmunityMultiplier);
    }
}

// ====================================================================
// RESOLVE ALL MODIFIERS
// Current sources:
// 1. Traits
// 2. Equipment
//
// Future sources can be added here:
// - Buffs
// - Debuffs
// - Injuries
// - Magical curses
// - Buildings / rooms / aura effects
// ====================================================================

FWorldForgeResolvedModifiers UWorldForgeModifierResolver::ResolveAgentModifiers(const FAgentData& Agent)
{
    FWorldForgeResolvedModifiers Result;

    // ====================================================================
    // TRAIT MODIFIERS
    // ====================================================================
    UDataTable* TraitTable = GetTraitTable();

    if (TraitTable && !Agent.TraitTags.IsEmpty())
    {
        for (const FGameplayTag& TraitTag : Agent.TraitTags)
        {
            FWorldForgeTraitDefinition Definition;

            if (!UWorldForgeTraitLibrary::FindTraitDefinition(TraitTable, TraitTag, Definition))
            {
                continue;
            }

            FWorldForgeResolvedModifiers TraitMods;
            TraitMods.WorkEfficiencyMultiplier = Definition.WorkEfficiencyMultiplier;
            TraitMods.HealingMultiplier = Definition.HealingMultiplier;
            TraitMods.InjuryChanceMultiplier = Definition.InjuryChanceMultiplier;
            TraitMods.ImmunityMultiplier = Definition.ImmunityMultiplier;

            TraitMods.MoodModifier = Definition.MoodModifier;
            TraitMods.MoraleDeltaPerStep = Definition.MoraleDeltaPerStep;
            TraitMods.SocialModifier = Definition.SocialModifier;
            TraitMods.LikeabilityModifier = Definition.LikeabilityModifier;
            TraitMods.AnnoyanceModifier = Definition.AnnoyanceModifier;
            TraitMods.SkillModifiers = Definition.SkillModifiers;

            ApplyResolvedModifiers(Result, TraitMods);
        }
    }

    // ====================================================================
    // EQUIPMENT MODIFIERS
    // ====================================================================
    UDataTable* EquipmentTable = UWorldForgeModifierResolver::GetEquipmentTable();

    if (EquipmentTable)
    {
        for (const TPair<EWorldForgeEquipmentSlot, FGameplayTag>& EquippedPair : Agent.EquippedItems)
        {
            const FGameplayTag EquipmentTag = EquippedPair.Value;

            if (!EquipmentTag.IsValid())
            {
                continue;
            }

            const FWorldForgeEquipmentDefinition* EquipmentDef =
                EquipmentTable->FindRow<FWorldForgeEquipmentDefinition>(
                    EquipmentTag.GetTagName(),
                    TEXT("ModifierResolver Equipment Lookup"),
                    false);

            if (!EquipmentDef)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ModifierResolver] Could not find equipment row for %s."),
                    *EquipmentTag.ToString());

                continue;
            }

            ApplyResolvedModifiers(Result, EquipmentDef->Modifiers);
        }
    }

    ClampResolvedMultipliers(Result);

    return Result;
}

// ====================================================================
// RESOLVE FINAL SKILL VALUE
// Base skill + traits + equipment + future buffs/debuffs.
// ====================================================================

float UWorldForgeModifierResolver::ResolveSkillValue(
    const FAgentData& Agent,
    FGameplayTag SkillTag)
{
    if (!SkillTag.IsValid())
    {
        return 0.0f;
    }

    const float* BaseSkillPtr = Agent.Skills.Find(SkillTag);
    const float BaseSkill = BaseSkillPtr ? *BaseSkillPtr : 0.0f;

    const FWorldForgeResolvedModifiers Mods = ResolveAgentModifiers(Agent);

    const float* SkillModPtr = Mods.SkillModifiers.Find(SkillTag);
    const float SkillMod = SkillModPtr ? *SkillModPtr : 0.0f;

    return FMath::Clamp(BaseSkill + SkillMod, 0.0f, 100.0f);
}

// ====================================================================
// RESOLVE FINAL WORK EFFICIENCY
// CurrentEfficiency * traits * equipment * future buffs/debuffs.
// ====================================================================

float UWorldForgeModifierResolver::ResolveWorkEfficiency(const FAgentData& Agent)
{
    const FWorldForgeResolvedModifiers Mods = ResolveAgentModifiers(Agent);

    return FMath::Clamp(
        Agent.CurrentEfficiency * Mods.WorkEfficiencyMultiplier,
        0.0f,
        2.0f);
}
#include "Visual/DA_VoxelVisualRuleSet.h"

bool UDA_VoxelVisualRuleSet::GetBuildingRule(FGameplayTag BuildingTag, FBuildingVisualRule& OutRule) const
{
    if (!BuildingTag.IsValid())
    {
        return false;
    }

    for (const FBuildingVisualRule& Rule : BuildingRules)
    {
        if (!Rule.BuildingTag.IsValid())
        {
            continue;
        }

        if (BuildingTag == Rule.BuildingTag || BuildingTag.MatchesTag(Rule.BuildingTag))
        {
            OutRule = Rule;
            return true;
        }
    }

    return false;
}

bool UDA_VoxelVisualRuleSet::GetBiomePropRule(FGameplayTag BiomeTag, FBiomePropRule& OutRule) const
{
    if (!BiomeTag.IsValid())
    {
        return false;
    }

    for (const FBiomePropRule& Rule : BiomePropRules)
    {
        if (!Rule.BiomeTag.IsValid())
        {
            continue;
        }

        if (BiomeTag == Rule.BiomeTag || BiomeTag.MatchesTag(Rule.BiomeTag))
        {
            OutRule = Rule;
            return true;
        }
    }

    return false;
}
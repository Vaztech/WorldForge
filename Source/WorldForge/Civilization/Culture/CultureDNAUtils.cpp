#include "Culture/CultureDNAUtils.h"

namespace CultureDNAUtils
{
    float GetAxisValue(const FCultureData& DNA, int32 Axis)
    {
        switch (Axis)
        {
        case CA_Hierarchy:         return DNA.HierarchyTolerance;
        case CA_OutgroupHostility: return DNA.OutgroupHostility;
        case CA_LaborEthics:       return DNA.LaborEthics;
        case CA_Militarism:        return DNA.Militarism;
        case CA_MagicReverence:    return DNA.MagicReverence;
        case CA_Individualism:     return DNA.Individualism;
        case CA_TheologicalRigid:  return DNA.TheologicalRigidity;
        case CA_EconomicOpenness:  return DNA.EconomicOpenness;
        case CA_ExpansionDrive:    return DNA.ExpansionDrive;
        case CA_CosmicDevotion:    return DNA.CosmicDevotion;
        default:                   return 0.0f;
        }
    }

    void SetAxisValue(FCultureData& DNA, int32 Axis, float Value)
    {
        switch (Axis)
        {
        case CA_Hierarchy:         DNA.HierarchyTolerance = Value; break;
        case CA_OutgroupHostility: DNA.OutgroupHostility = Value; break;
        case CA_LaborEthics:       DNA.LaborEthics = Value; break;
        case CA_Militarism:        DNA.Militarism = Value; break;
        case CA_MagicReverence:    DNA.MagicReverence = Value; break;
        case CA_Individualism:     DNA.Individualism = Value; break;
        case CA_TheologicalRigid:  DNA.TheologicalRigidity = Value; break;
        case CA_EconomicOpenness:  DNA.EconomicOpenness = Value; break;
        case CA_ExpansionDrive:    DNA.ExpansionDrive = Value; break;
        case CA_CosmicDevotion:    DNA.CosmicDevotion = Value; break;
        default: break;
        }
    }

    float DriftAxis(float Current, float Delta)
    {
        return FMath::Clamp(Current + Delta, 0.0f, 100.0f);
    }
}
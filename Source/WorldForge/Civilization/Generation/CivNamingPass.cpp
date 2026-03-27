#include "CivNamingPass.h"

void FCivNamingPass::AssignDefaultName(FCivilizationData& Civ)
{
    // KEPT EXISTING: preserve exact current default naming behavior
    Civ.Name = FString::Printf(TEXT("Civilization_%d"), Civ.CivID);
}
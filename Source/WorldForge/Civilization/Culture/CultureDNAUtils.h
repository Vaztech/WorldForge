#pragma once

#include "CoreMinimal.h"
#include "WorldForgeDataTypes.h"

namespace CultureDNAUtils
{
    float GetAxisValue(const FCultureData& DNA, int32 Axis);
    void SetAxisValue(FCultureData& DNA, int32 Axis, float Value);
    float DriftAxis(float Current, float Delta);
}
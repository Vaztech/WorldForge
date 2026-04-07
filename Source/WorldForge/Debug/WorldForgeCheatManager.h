#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "WorldForgeCheatManager.generated.h"

UCLASS()
class WORLDFORGE_API UWorldForgeCheatManager : public UCheatManager
{
    GENERATED_BODY()

public:
    UFUNCTION(Exec)
    void Add10Stone(int32 SettlementID = 0);

    UFUNCTION(Exec)
    void Add100All(int32 SettlementID = 0);

    UFUNCTION(Exec)
    void PrintStockpile(int32 SettlementID = 0);

    // OPTIONAL DEBUG COMMAND
    // NOTE: Temporary helper for testing. Remove or lock down before shipping.
    UFUNCTION(Exec)
    void AddResource(const FString& Resource, int32 Amount, int32 SettlementID = 0);
};
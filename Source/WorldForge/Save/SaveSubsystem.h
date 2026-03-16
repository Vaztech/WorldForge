#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldForgeSaveGame.h"
#include "SaveSubsystem.generated.h"

class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveComplete, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadComplete, bool, bSuccess);

UCLASS()
class WORLDFORGE_API USaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable) void SaveGame(const FString& SlotName, UWorld* World);
    UFUNCTION(BlueprintCallable) void LoadGame(const FString& SlotName, UWorld* World);

    UFUNCTION(BlueprintCallable) TArray<FString> GetSaveSlots() const;
    UFUNCTION(BlueprintCallable) bool SaveSlotExists(const FString& SlotName) const;
    UFUNCTION(BlueprintCallable) void DeleteSaveSlot(const FString& SlotName);

    UPROPERTY(BlueprintAssignable) FOnSaveComplete OnSaveComplete;
    UPROPERTY(BlueprintAssignable) FOnLoadComplete OnLoadComplete;

    bool IsSaving()  const { return bSaveInProgress; }
    bool IsLoading() const { return bLoadInProgress; }

private:
    UWorldForgeSaveGame* CollectWorldState(UWorld* W, const FString& Slot);
    void DistributeLoadedState(UWorldForgeSaveGame* Data, UWorld* W);

    UFUNCTION() void OnSaveCompleted(const FString& SlotName, const int32 UserIndex, bool bSuccess);

    bool bSaveInProgress = false;
    bool bLoadInProgress = false;
};
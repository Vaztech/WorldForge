#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldForgeSaveGame.h"
#include "SaveSubsystem.generated.h"

class UObject;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveComplete, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadComplete, bool, bSuccess);

UCLASS(BlueprintType)
class WORLDFORGE_API USaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    void SaveGame(const FString& SlotName, UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    void LoadGame(const FString& SlotName, UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    TArray<FString> GetSaveSlots() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    bool SaveSlotExists(const FString& SlotName) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    void DeleteSaveSlot(const FString& SlotName);

    UPROPERTY(BlueprintAssignable, Category = "WorldForge|Save")
    FOnSaveComplete OnSaveComplete;

    UPROPERTY(BlueprintAssignable, Category = "WorldForge|Save")
    FOnLoadComplete OnLoadComplete;

    bool IsSaving() const { return bSaveInProgress; }
    bool IsLoading() const { return bLoadInProgress; }

private:
    UWorldForgeSaveGame* CollectWorldState(UWorld* W, const FString& Slot);
    void DistributeLoadedState(UWorldForgeSaveGame* Data, UWorld* W);

    UFUNCTION()
    void OnSaveCompleted(const FString& SlotName, const int32 UserIndex, bool bSuccess);

    bool bSaveInProgress = false;
    bool bLoadInProgress = false;
};
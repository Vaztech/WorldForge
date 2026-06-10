#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldForgeSaveGame.h"
#include "WorldEconomySimulation.h"
#include "SaveSubsystem.generated.h"

class UObject;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveComplete, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadComplete, bool, bSuccess);

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeSaveSlotSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    int32 SlotIndex = 0;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString SaveSlotName;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString WorldName;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString ColonyName;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString LastPlayedText;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString DayYearText;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    FString SaveVersionText;

    UPROPERTY(BlueprintReadOnly, Category = "WorldForge|Save")
    bool bHasSave = false;
};

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
    TArray<FWorldForgeSaveSlotSummary> GetFixedSaveSlotSummaries(int32 MaxSlots = 8) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    bool SaveSlotExists(const FString& SlotName) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Save")
    bool DeleteSaveSlot(const FString& SaveSlotName, FString& OutReason);

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

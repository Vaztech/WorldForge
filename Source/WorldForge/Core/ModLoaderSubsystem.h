#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ModLoaderSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FModManifest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString ModID;
    UPROPERTY(BlueprintReadOnly) FString ModName;
    UPROPERTY(BlueprintReadOnly) FString Version;
    UPROPERTY(BlueprintReadOnly) FString Author;
    UPROPERTY(BlueprintReadOnly) TArray<FString> DeclaredRaces;
    UPROPERTY(BlueprintReadOnly) TArray<FString> DeclaredBiomes;
    UPROPERTY(BlueprintReadOnly) TArray<FString> DeclaredCurrencies;
    UPROPERTY(BlueprintReadOnly) int32 Priority = 100;
    UPROPERTY(BlueprintReadOnly) bool bIsLoaded = false;
};

UCLASS()
class WORLDFORGE_API UModLoaderSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Scans /mods folder and loads all valid mods
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Mods")
    void ScanAndLoadMods();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Mods")
    TArray<FModManifest> GetLoadedMods() const { return LoadedMods; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Mods")
    int32 GetLoadedModCount() const { return LoadedMods.Num(); }

private:
    TArray<FModManifest> LoadedMods;
    FString ModsFolderPath;

    bool ParseModManifest(const FString& JsonPath, FModManifest& OutManifest);
    bool MountModPak(const FString& PakPath);
    void RegisterModAssets(const FModManifest& Manifest);
};
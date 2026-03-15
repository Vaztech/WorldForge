#include "ModLoaderSubsystem.h"
#include "../Data/WorldForgeDataRegistry.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UModLoaderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Mods folder lives at the project root level
    ModsFolderPath = FPaths::ProjectDir() + TEXT("mods/");

    UE_LOG(LogTemp, Log, TEXT("[ModLoader] Initialized. Mods folder: %s"), *ModsFolderPath);

    ScanAndLoadMods();
}

void UModLoaderSubsystem::ScanAndLoadMods()
{
    LoadedMods.Empty();

    if (!IFileManager::Get().DirectoryExists(*ModsFolderPath))
    {
        UE_LOG(LogTemp, Log, TEXT("[ModLoader] No mods folder found at %s - skipping."), *ModsFolderPath);
        return;
    }

    // Find all subdirectories in /mods/
    TArray<FString> ModDirs;
    IFileManager::Get().FindFiles(ModDirs, *(ModsFolderPath + TEXT("*")), false, true);

    UE_LOG(LogTemp, Log, TEXT("[ModLoader] Found %d mod directories."), ModDirs.Num());

    for (const FString& DirName : ModDirs)
    {
        const FString ModDir = ModsFolderPath + DirName + TEXT("/");
        const FString ManifestPath = ModDir + TEXT("mod.json");

        FModManifest Manifest;

        if (!ParseModManifest(ManifestPath, Manifest))
        {
            UE_LOG(LogTemp, Warning, TEXT("[ModLoader] %s: missing or invalid mod.json - skipped."), *DirName);
            continue;
        }

        // Try to mount .pak file if present
        const FString PakPath = ModDir + DirName + TEXT(".pak");

        if (IFileManager::Get().FileExists(*PakPath))
        {
            MountModPak(PakPath);
        }

        Manifest.bIsLoaded = true;
        LoadedMods.Add(Manifest);

        UE_LOG(LogTemp, Log, TEXT("[ModLoader] Loaded mod: %s v%s by %s"),
            *Manifest.ModName,
            *Manifest.Version,
            *Manifest.Author);
    }

    UE_LOG(LogTemp, Log, TEXT("[ModLoader] Total mods loaded: %d"), LoadedMods.Num());
}

bool UModLoaderSubsystem::ParseModManifest(const FString& JsonPath, FModManifest& OutManifest)
{
    FString JsonString;

    if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    OutManifest.ModID = JsonObject->GetStringField(TEXT("mod_id"));
    OutManifest.ModName = JsonObject->GetStringField(TEXT("name"));
    OutManifest.Version = JsonObject->GetStringField(TEXT("version"));
    OutManifest.Author = JsonObject->GetStringField(TEXT("author"));

    if (JsonObject->HasField(TEXT("priority")))
    {
        OutManifest.Priority = (int32)JsonObject->GetNumberField(TEXT("priority"));
    }

    return !OutManifest.ModID.IsEmpty();
}

bool UModLoaderSubsystem::MountModPak(const FString& PakPath)
{
    // TODO Phase 4: Use IPlatformFile to mount .pak file
    UE_LOG(LogTemp, Log, TEXT("[ModLoader] Pak mount stub: %s"), *PakPath);

    return true;
}

void UModLoaderSubsystem::RegisterModAssets(const FModManifest& Manifest)
{
    // TODO Phase 4: Walk mod content folder and register DataAssets with UWorldForgeDataRegistry
    UE_LOG(LogTemp, Log, TEXT("[ModLoader] Asset registration stub for mod: %s"), *Manifest.ModID);
}
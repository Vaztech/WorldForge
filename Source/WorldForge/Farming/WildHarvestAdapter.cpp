// Source/WorldForge/Farming/WildHarvestAdapter.cpp
//
// Implementation of the Wild Harvest C++↔BP bridge.
// All BP variable names and BPI function names come from Asset Discovery.
//
// Notes from runtime reflection:
// - BP_CropPatch_LowPoly_C exposes direct no-param functions:
//   WH_Interact, WH_Water, WH_Fertilize, WH_RefreshConfig,
//   WH_RefreshCropConfig, Update Growth Timer, Next Growth Stage.
// - Interface functions also exist, but WH_Interface_Water has params,
//   so the no-param WH_Water path is safer for C++ automation.
//
// Important runtime rule:
// Do NOT call WH_RefreshConfig / WH_RefreshCropConfig after planting,
// watering, growth advancement, harvesting, or fertilizing. Those functions
// appear to reload/reset Wild Harvest config and can fight WorldForge's
// sim-step-driven crop state.
// ════════════════════════════════════════════════════════════════════

#include "Farming/WildHarvestAdapter.h"

#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Data/WorldForgeTags.h"
#include "UObject/UnrealType.h"
#include "Components/PrimitiveComponent.h"

// ═══════════════════════════════════════════════════════════════════
// Local BP write / reflection helpers
// Kept in .cpp so WildHarvestAdapter.h does not need new declarations.
// ═══════════════════════════════════════════════════════════════════

namespace
{
    static bool WF_WriteBPFloat(AActor* Actor, FName VarName, float Value)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        if (FFloatProperty* FloatProp = CastField<FFloatProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            FloatProp->SetPropertyValue(
                FloatProp->ContainerPtrToValuePtr<void>(Actor),
                Value);

            return true;
        }

        if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            DoubleProp->SetPropertyValue(
                DoubleProp->ContainerPtrToValuePtr<void>(Actor),
                static_cast<double>(Value));

            return true;
        }

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] WF_WriteBPFloat failed: Property '%s' not found on %s"),
            *VarName.ToString(),
            *Actor->GetName());

        return false;
    }

    static bool WF_WriteBPBool(AActor* Actor, FName VarName, bool Value)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        if (FBoolProperty* BoolProp = CastField<FBoolProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            BoolProp->SetPropertyValue(
                BoolProp->ContainerPtrToValuePtr<void>(Actor),
                Value);

            return true;
        }

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] WF_WriteBPBool failed: Property '%s' not found on %s"),
            *VarName.ToString(),
            *Actor->GetName());

        return false;
    }

    static bool WF_WriteBPInt(AActor* Actor, FName VarName, int32 Value)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        if (FIntProperty* IntProp = CastField<FIntProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            IntProp->SetPropertyValue(
                IntProp->ContainerPtrToValuePtr<void>(Actor),
                Value);

            return true;
        }

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] WF_WriteBPInt failed: Property '%s' not found on %s"),
            *VarName.ToString(),
            *Actor->GetName());

        return false;
    }

    static bool WF_WriteBPByte(AActor* Actor, FName VarName, uint8 Value)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        if (FByteProperty* ByteProp = CastField<FByteProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            *ByteProp->ContainerPtrToValuePtr<uint8>(Actor) = Value;
            return true;
        }

        if (FEnumProperty* EnumProp = CastField<FEnumProperty>(
            Actor->GetClass()->FindPropertyByName(VarName)))
        {
            if (FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty())
            {
                UnderlyingProp->SetIntPropertyValue(
                    EnumProp->ContainerPtrToValuePtr<void>(Actor),
                    static_cast<int64>(Value));

                return true;
            }
        }

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] WF_WriteBPByte failed: Property '%s' not found on %s"),
            *VarName.ToString(),
            *Actor->GetName());

        return false;
    }

    static bool WF_CallNoParamFunction(AActor* Actor, FName FuncName, bool bLogMissing = false)
    {
        if (!IsValid(Actor) ||
            Actor->IsActorBeingDestroyed() ||
            Actor->IsPendingKillPending())
        {
            return false;
        }

        UFunction* Func = Actor->FindFunction(FuncName);
        if (!Func)
        {
            if (bLogMissing)
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("[WildHarvestAdapter] No-param function '%s' not found on %s"),
                    *FuncName.ToString(),
                    *Actor->GetName());
            }

            return false;
        }

        if (Func->ParmsSize > 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[WildHarvestAdapter] No-param call skipped: function '%s' on %s has ParmsSize=%d"),
                *FuncName.ToString(),
                *Actor->GetName(),
                Func->ParmsSize);

            return false;
        }

        Actor->ProcessEvent(Func, nullptr);

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] Called no-param BP function '%s' on %s"),
            *FuncName.ToString(),
            *Actor->GetName());

        return true;
    }

    static bool WF_CallHarvestFunction(AActor* Actor, FName InterfaceHarvestFuncName)
    {
        if (!IsValid(Actor) ||
            Actor->IsActorBeingDestroyed() ||
            Actor->IsPendingKillPending())
        {
            return false;
        }

        // Prefer direct interface harvest because it is the documented Wild Harvest path.
        UFunction* Func = Actor->FindFunction(InterfaceHarvestFuncName);
        if (Func)
        {
            TArray<uint8> ParamsBuffer;
            void* Params = nullptr;

            if (Func->ParmsSize > 0)
            {
                ParamsBuffer.SetNumZeroed(Func->ParmsSize);
                Params = ParamsBuffer.GetData();
            }

            Actor->ProcessEvent(Func, Params);

            UE_LOG(LogTemp, Verbose,
                TEXT("[WildHarvestAdapter] Called harvest function '%s' on %s ParmsSize=%d"),
                *InterfaceHarvestFuncName.ToString(),
                *Actor->GetName(),
                Func->ParmsSize);

            return true;
        }

        // Some packs expose direct no-param harvest variants. Try safe common names.
        static const TArray<FName> DirectHarvestCandidates =
        {
            FName(TEXT("WH_Harvest")),
            FName(TEXT("Harvest")),
            FName(TEXT("Harvest Crop"))
        };

        for (const FName& Candidate : DirectHarvestCandidates)
        {
            if (WF_CallNoParamFunction(Actor, Candidate, false))
            {
                return true;
            }
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] No harvest function found on %s. Tried interface '%s' and direct candidates."),
            *Actor->GetName(),
            *InterfaceHarvestFuncName.ToString());

        return false;
    }

    static void WF_DumpCropReflectionOnce(AActor* Actor)
    {
        if (!IsValid(Actor))
        {
            return;
        }

        static TSet<FName> DumpedClasses;

        UClass* Class = Actor->GetClass();
        if (!Class)
        {
            return;
        }

        const FName ClassName = Class->GetFName();
        if (DumpedClasses.Contains(ClassName))
        {
            return;
        }

        DumpedClasses.Add(ClassName);

        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] ===== BP REFLECTION DUMP BEGIN Class=%s Actor=%s ====="),
            *Class->GetName(),
            *Actor->GetName());

        for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Prop = *It;
            if (!Prop)
            {
                continue;
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[WildHarvestAdapter] BP Property: Name='%s' Type='%s'"),
                *Prop->GetName(),
                *Prop->GetClass()->GetName());
        }

        for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const UFunction* Func = *It;
            if (!Func)
            {
                continue;
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[WildHarvestAdapter] BP Function: Name='%s' ParmsSize=%d"),
                *Func->GetName(),
                Func->ParmsSize);
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] ===== BP REFLECTION DUMP END Class=%s ====="),
            *Class->GetName());
    }
}

// ═══════════════════════════════════════════════════════════════════
// Static FName Constants
// ═══════════════════════════════════════════════════════════════════

const FName UWildHarvestAdapter::BPVar_CropState = FName(TEXT("Crop State"));
const FName UWildHarvestAdapter::BPVar_WaterLevel = FName(TEXT("Water Level"));
const FName UWildHarvestAdapter::BPVar_IsFertilized = FName(TEXT("Is fertilized"));
const FName UWildHarvestAdapter::BPVar_IsDroughted = FName(TEXT("Is Droughted"));
const FName UWildHarvestAdapter::BPVar_GrowthStageIndex = FName(TEXT("Growth Stage Index"));
const FName UWildHarvestAdapter::BPVar_CropDataTable = FName(TEXT("Crop Data Table"));
const FName UWildHarvestAdapter::BPVar_CropRowName = FName(TEXT("Crop Row Name"));
const FName UWildHarvestAdapter::BPVar_DropAmount = FName(TEXT("Drop Amount"));

const FName UWildHarvestAdapter::BPIFunc_Interact = FName(TEXT("WH_Interface_Interact"));
const FName UWildHarvestAdapter::BPIFunc_Water = FName(TEXT("WH_Interface_Water"));
const FName UWildHarvestAdapter::BPIFunc_Harvest = FName(TEXT("WH_Interface_Harvest"));
const FName UWildHarvestAdapter::BPIFunc_Fertilize = FName(TEXT("WH_Interface_Fertilize"));

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void UWildHarvestAdapter::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    CropDataTable = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[WildHarvestAdapter] Initialized."));
}

void UWildHarvestAdapter::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("[WildHarvestAdapter] Deinitialized."));

    Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════

void UWildHarvestAdapter::SetCropDataTable(UDataTable* Table)
{
    CropDataTable = Table;

    UE_LOG(LogTemp, Log,
        TEXT("[WildHarvestAdapter] DataTable set: %s"),
        Table ? *Table->GetName() : TEXT("null"));
}

void UWildHarvestAdapter::SetCropPatchClass(TSubclassOf<AActor> InClass)
{
    CropPatchClass = InClass;

    UE_LOG(LogTemp, Log,
        TEXT("[WildHarvestAdapter] CropPatch class set: %s"),
        InClass ? *InClass->GetName() : TEXT("null"));
}

bool UWildHarvestAdapter::IsConfigured() const
{
    return CropDataTable != nullptr && CropPatchClass != nullptr;
}

TArray<FName> UWildHarvestAdapter::GetAllCropRowNames() const
{
    TArray<FName> Result;

    if (CropDataTable)
    {
        Result = CropDataTable->GetRowNames();
    }

    return Result;
}

// ═══════════════════════════════════════════════════════════════════
// Spawn / Despawn
// ═══════════════════════════════════════════════════════════════════

AActor* UWildHarvestAdapter::SpawnCropPatch(FName CropRow, FVector WorldPos)
{
    UWorld* World = GetWorld();

    if (CropRow.IsNone() || !CropDataTable || !CropDataTable->FindRowUnchecked(CropRow))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] SpawnCropPatch blocked: invalid or missing crop row. Row=%s DataTable=%s"),
            *CropRow.ToString(),
            CropDataTable ? *CropDataTable->GetName() : TEXT("null"));

        return nullptr;
    }

    if (!World || !CropPatchClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] SpawnCropPatch: World=%s Class=%s"),
            World ? TEXT("valid") : TEXT("null"),
            CropPatchClass ? TEXT("valid") : TEXT("null"));

        return nullptr;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* Actor = World->SpawnActor<AActor>(
        CropPatchClass,
        FTransform(FRotator::ZeroRotator, WorldPos),
        Params);

    if (!Actor)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] SpawnCropPatch failed for Row=%s at %s"),
            *CropRow.ToString(),
            *WorldPos.ToString());

        return nullptr;
    }

    WF_DumpCropReflectionOnce(Actor);

    FObjectProperty* DTProp = CastField<FObjectProperty>(
        Actor->GetClass()->FindPropertyByName(BPVar_CropDataTable));

    if (DTProp && CropDataTable)
    {
        DTProp->SetObjectPropertyValue(
            DTProp->ContainerPtrToValuePtr<void>(Actor),
            CropDataTable);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] Could not set '%s' on %s. Verify BP variable name in BP_CropPatch_Master."),
            *BPVar_CropDataTable.ToString(),
            *Actor->GetName());
    }

    FNameProperty* RowProp = CastField<FNameProperty>(
        Actor->GetClass()->FindPropertyByName(BPVar_CropRowName));

    if (RowProp)
    {
        FName* ValuePtr = RowProp->ContainerPtrToValuePtr<FName>(Actor);
        *ValuePtr = CropRow;
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] Could not set '%s' on %s."),
            *BPVar_CropRowName.ToString(),
            *Actor->GetName());
    }

    // IMPORTANT:
    // The Wild Harvest crop BP builds its visible meshes/config from
    // "Crop Data Table" + "Crop Row Name".  The BP construction path can
    // initialize using its default row before C++ writes the selected row above.
    //
    // Call WH_RefreshConfig (on BP_CropPatch_Master) directly.
    // Do NOT call WH_RefreshCropConfig — the LowPoly child BP overrides that
    // event to re-derive "Crop Row Name" from its own "Crop Types" enum,
    // which overwrites the row name C++ just set and causes every crop to
    // display as the enum default (Wheat).
    //
    // WH_RefreshConfig reads the current "Crop Row Name" property (already set
    // above) and reloads the correct visual config from the DataTable.
    //
    // Do not refresh later during water/growth/harvest, because those later
    // calls can reset runtime crop state.
    const bool bConfigRefreshed = WF_CallNoParamFunction(
        Actor,
        FName(TEXT("WH_RefreshConfig")),
        true);

    UE_LOG(LogTemp, Log,
        TEXT("[WildHarvestAdapter] Crop config refresh after row assignment. Actor=%s Row=%s Refreshed=%s"),
        *Actor->GetName(),
        *CropRow.ToString(),
        bConfigRefreshed ? TEXT("true") : TEXT("false"));

    // Diagnostic: read back "Crop Row Name" after refresh to confirm
    // the BP did not overwrite it (the old WH_RefreshCropConfig bug).
    if (RowProp)
    {
        const FName* VerifyPtr = RowProp->ContainerPtrToValuePtr<FName>(Actor);
        const FName VerifiedRow = VerifyPtr ? *VerifyPtr : NAME_None;

        if (VerifiedRow != CropRow)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[WildHarvestAdapter] BUG: Crop Row Name was overwritten after refresh! Expected=%s Actual=%s Actor=%s"),
                *CropRow.ToString(),
                *VerifiedRow.ToString(),
                *Actor->GetName());

            // Force-restore the correct row name.
            *RowProp->ContainerPtrToValuePtr<FName>(Actor) = CropRow;
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("[WildHarvestAdapter] Crop Row Name verified after refresh. Row=%s Actor=%s"),
                *CropRow.ToString(),
                *Actor->GetName());
        }
    }

    Actor->SetActorEnableCollision(false);
    Actor->SetActorLocation(WorldPos, false, nullptr, ETeleportType::TeleportPhysics);

    UPrimitiveComponent* BestBaseComponent = nullptr;
    float BestFlatArea = -1.0f;

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    for (UPrimitiveComponent* Prim : PrimitiveComponents)
    {
        if (!IsValid(Prim))
        {
            continue;
        }

        const FString CompName = Prim->GetName();

        const bool bNameLooksLikeBase =
            CompName.Contains(TEXT("Soil"), ESearchCase::IgnoreCase) ||
            CompName.Contains(TEXT("Dirt"), ESearchCase::IgnoreCase) ||
            CompName.Contains(TEXT("Plot"), ESearchCase::IgnoreCase) ||
            CompName.Contains(TEXT("Ground"), ESearchCase::IgnoreCase) ||
            CompName.Contains(TEXT("Plane"), ESearchCase::IgnoreCase);

        const FBoxSphereBounds Bounds = Prim->Bounds;
        const float FlatArea = Bounds.BoxExtent.X * Bounds.BoxExtent.Y;
        const float Height = Bounds.BoxExtent.Z * 2.0f;
        const bool bFlatEnough = Height <= 120.0f;

        if ((bNameLooksLikeBase || bFlatEnough) && FlatArea > BestFlatArea)
        {
            BestFlatArea = FlatArea;
            BestBaseComponent = Prim;
        }
    }

    if (BestBaseComponent)
    {
        const FBoxSphereBounds Bounds = BestBaseComponent->Bounds;
        const float ComponentTopZ = Bounds.Origin.Z + Bounds.BoxExtent.Z;

        // Raise this if the soil is buried. Lower it if the soil floats.
        const float DesiredTopZ = WorldPos.Z + 11.0f;
        const float DeltaZ = DesiredTopZ - ComponentTopZ;

        FVector AdjustedLocation = Actor->GetActorLocation();
        AdjustedLocation.Z += DeltaZ;

        Actor->SetActorLocation(AdjustedLocation, false, nullptr, ETeleportType::TeleportPhysics);

        UE_LOG(LogTemp, Log,
            TEXT("[WildHarvestAdapter] Aligned crop patch TOP to terrain. Actor=%s BaseComponent=%s TerrainZ=%.2f TopZ=%.2f DeltaZ=%.2f FinalLocation=%s"),
            *Actor->GetName(),
            *BestBaseComponent->GetName(),
            WorldPos.Z,
            ComponentTopZ,
            DeltaZ,
            *Actor->GetActorLocation().ToString());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] Could not find crop base component for %s. Actor left at terrain hit point."),
            *Actor->GetName());
    }

    // Initial fallback values only. Do not call WH_RefreshConfig or
    // WH_RefreshCropConfig here because those can reset runtime crop state.
    WF_WriteBPFloat(Actor, BPVar_WaterLevel, 1.0f);
    WF_WriteBPBool(Actor, BPVar_IsDroughted, false);
    WF_WriteBPByte(Actor, BPVar_CropState, 1);
    WF_WriteBPInt(Actor, BPVar_GrowthStageIndex, 0);

    UE_LOG(LogTemp, Log,
        TEXT("[WildHarvestAdapter] Spawned '%s' (Row=%s) at %s"),
        *Actor->GetName(),
        *CropRow.ToString(),
        *Actor->GetActorLocation().ToString());

    return Actor;
}

void UWildHarvestAdapter::DespawnCropPatch(AActor* CropActor)
{
    if (IsValid(CropActor) &&
        !CropActor->IsActorBeingDestroyed() &&
        !CropActor->IsPendingKillPending())
    {
        CropActor->Destroy();
    }
}

// ═══════════════════════════════════════════════════════════════════
// BPI Interface Calls
// ═══════════════════════════════════════════════════════════════════

bool UWildHarvestAdapter::InteractWithCrop(AActor* CropActor)
{
    bool bCalled = false;

    // Exact no-param function discovered in BP_CropPatch_LowPoly_C.
    bCalled |= WF_CallNoParamFunction(CropActor, FName(TEXT("WH_Interact")), true);

    // Fallback to interface call if the direct function is unavailable.
    if (!bCalled)
    {
        bCalled = CallBPIFunction(CropActor, BPIFunc_Interact);
    }

    if (IsValid(CropActor))
    {
        WF_WriteBPFloat(CropActor, BPVar_WaterLevel, 1.0f);
        WF_WriteBPBool(CropActor, BPVar_IsDroughted, false);
        WF_WriteBPByte(CropActor, BPVar_CropState, 1);

        int32 CurrentGrowthIndex = INDEX_NONE;
        if (!ReadBPInt(CropActor, BPVar_GrowthStageIndex, CurrentGrowthIndex) ||
            CurrentGrowthIndex < 0)
        {
            WF_WriteBPInt(CropActor, BPVar_GrowthStageIndex, 0);
        }

        UE_LOG(LogTemp, Log,
            TEXT("[WildHarvestAdapter] Interact state applied. Actor=%s Called=%s Water=1.00 Droughted=false"),
            *CropActor->GetName(),
            bCalled ? TEXT("true") : TEXT("false"));
    }

    return bCalled;
}

bool UWildHarvestAdapter::WaterCrop(AActor* CropActor)
{
    bool bCalled = false;

    // Exact no-param function discovered in BP_CropPatch_LowPoly_C.
    // This is preferred over WH_Interface_Water because WH_Interface_Water has params.
    bCalled |= WF_CallNoParamFunction(CropActor, FName(TEXT("WH_Water")), true);

    // Fallback only if direct no-param function is unavailable.
    if (!bCalled)
    {
        bCalled = CallBPIFunction(CropActor, BPIFunc_Water);
    }

    if (IsValid(CropActor))
    {
        const bool bWroteWater = WF_WriteBPFloat(CropActor, BPVar_WaterLevel, 1.0f);
        const bool bWroteDrought = WF_WriteBPBool(CropActor, BPVar_IsDroughted, false);
        const bool bWroteState = WF_WriteBPByte(CropActor, BPVar_CropState, 1);

        UE_LOG(LogTemp, Log,
            TEXT("[WildHarvestAdapter] Water state applied. Actor=%s Called=%s WroteWater=%s WroteDrought=%s WroteState=%s"),
            *CropActor->GetName(),
            bCalled ? TEXT("true") : TEXT("false"),
            bWroteWater ? TEXT("true") : TEXT("false"),
            bWroteDrought ? TEXT("true") : TEXT("false"),
            bWroteState ? TEXT("true") : TEXT("false"));

        return bCalled || bWroteWater || bWroteDrought || bWroteState;
    }

    return bCalled;
}

bool UWildHarvestAdapter::HarvestCrop(AActor* CropActor)
{
    const bool bCalled = WF_CallHarvestFunction(CropActor, BPIFunc_Harvest);

    if (IsValid(CropActor))
    {
        WF_WriteBPByte(CropActor, BPVar_CropState, 3);
        WF_WriteBPFloat(CropActor, BPVar_WaterLevel, 0.0f);

        UE_LOG(LogTemp, Log,
            TEXT("[WildHarvestAdapter] Harvest state applied. Actor=%s Called=%s"),
            *CropActor->GetName(),
            bCalled ? TEXT("true") : TEXT("false"));
    }

    return bCalled;
}

bool UWildHarvestAdapter::FertilizeCrop(AActor* CropActor)
{
    bool bCalled = false;

    bCalled |= WF_CallNoParamFunction(CropActor, FName(TEXT("WH_Fertilize")), true);

    if (!bCalled)
    {
        bCalled = CallBPIFunction(CropActor, BPIFunc_Fertilize);
    }

    if (IsValid(CropActor))
    {
        WF_WriteBPBool(CropActor, BPVar_IsFertilized, true);

        UE_LOG(LogTemp, Log,
            TEXT("[WildHarvestAdapter] Fertilize state applied. Actor=%s Called=%s"),
            *CropActor->GetName(),
            bCalled ? TEXT("true") : TEXT("false"));
    }

    return bCalled;
}

bool UWildHarvestAdapter::AdvanceCropGrowth(AActor* CropActor)
{
    if (!IsValid(CropActor) ||
        CropActor->IsActorBeingDestroyed() ||
        CropActor->IsPendingKillPending())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] AdvanceCropGrowth skipped: invalid crop actor."));

        return false;
    }

    int32 OldGrowthIndex = INDEX_NONE;
    ReadBPInt(CropActor, BPVar_GrowthStageIndex, OldGrowthIndex);

    bool bAdvanced = false;

    // Exact no-param function discovered in BP_CropPatch_LowPoly_C.
    // Do not call WH_RefreshConfig / WH_RefreshCropConfig after this.
    bAdvanced |= WF_CallNoParamFunction(CropActor, FName(TEXT("Next Growth Stage")), true);

    int32 NewGrowthIndex = INDEX_NONE;
    ReadBPInt(CropActor, BPVar_GrowthStageIndex, NewGrowthIndex);

    // If the BP function did not advance the stored index, drive it manually.
    if (NewGrowthIndex <= OldGrowthIndex)
    {
        const int32 FallbackOld = OldGrowthIndex == INDEX_NONE ? 0 : OldGrowthIndex;
        NewGrowthIndex = FMath::Clamp(FallbackOld + 1, 0, 4);
        bAdvanced |= WF_WriteBPInt(CropActor, BPVar_GrowthStageIndex, NewGrowthIndex);
    }

    // Keep the BP in a valid growing state while advancing.
    // Do not overwrite to growing if final stage is reached; the subsystem maps
    // GrowthStageIndex >= 4 to Ready.
    WF_WriteBPBool(CropActor, BPVar_IsDroughted, false);

    if (NewGrowthIndex >= 4)
    {
        WF_WriteBPByte(CropActor, BPVar_CropState, 2);
    }
    else
    {
        WF_WriteBPByte(CropActor, BPVar_CropState, 1);
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[WildHarvestAdapter] AdvanceCropGrowth. Actor=%s OldGrowthIndex=%d NewGrowthIndex=%d Success=%s"),
        *CropActor->GetName(),
        OldGrowthIndex,
        NewGrowthIndex,
        bAdvanced ? TEXT("true") : TEXT("false"));

    return bAdvanced;
}

bool UWildHarvestAdapter::CallBPIFunction(AActor* Actor, FName FuncName) const
{
    if (!IsValid(Actor) ||
        Actor->IsActorBeingDestroyed() ||
        Actor->IsPendingKillPending())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] CallBPIFunction skipped: invalid actor for function '%s'."),
            *FuncName.ToString());

        return false;
    }

    UFunction* Func = Actor->FindFunction(FuncName);

    if (!Func)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] Function '%s' not found on %s. Open the BPI asset in the editor and verify the function name."),
            *FuncName.ToString(),
            *Actor->GetName());

        return false;
    }

    TArray<uint8> ParamsBuffer;
    void* Params = nullptr;

    if (Func->ParmsSize > 0)
    {
        ParamsBuffer.SetNumZeroed(Func->ParmsSize);
        Params = ParamsBuffer.GetData();
    }

    Actor->ProcessEvent(Func, Params);

    UE_LOG(LogTemp, Verbose,
        TEXT("[WildHarvestAdapter] Called '%s' on %s ParmsSize=%d"),
        *FuncName.ToString(),
        *Actor->GetName(),
        Func->ParmsSize);

    return true;
}

// ═══════════════════════════════════════════════════════════════════
// State Polling
// ═══════════════════════════════════════════════════════════════════

ECropGrowthStage UWildHarvestAdapter::GetCropState(AActor* CropActor) const
{
    if (!IsValid(CropActor) ||
        CropActor->IsActorBeingDestroyed() ||
        CropActor->IsPendingKillPending())
    {
        return ECropGrowthStage::None;
    }

    int32 GrowthIndex = INDEX_NONE;
    const bool bHasGrowthIndex = ReadBPInt(CropActor, BPVar_GrowthStageIndex, GrowthIndex);

    uint8 BPState = 255;
    const bool bHasBPState = ReadBPByte(CropActor, BPVar_CropState, BPState);

    // Your BP reflection dump did not expose "Ready To Harvest Stage" as a direct actor property.
    // LowPoly rows have five growth meshes; use the final visible mesh as ready by default.
    int32 ReadyToHarvestStage = 4;

    if (FProperty* ReadyProp = CropActor->GetClass()->FindPropertyByName(FName(TEXT("Ready To Harvest Stage"))))
    {
        if (FIntProperty* IntProp = CastField<FIntProperty>(ReadyProp))
        {
            ReadyToHarvestStage = IntProp->GetPropertyValue(
                IntProp->ContainerPtrToValuePtr<void>(CropActor));
        }
    }

    if (bHasGrowthIndex)
    {
        ECropGrowthStage MappedStage = ECropGrowthStage::None;

        if (GrowthIndex >= ReadyToHarvestStage)
        {
            MappedStage = ECropGrowthStage::Ready;
        }
        else if (GrowthIndex >= 0)
        {
            MappedStage = ECropGrowthStage::Growing;
        }

        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] CropState poll. Actor=%s GrowthIndex=%d ReadyToHarvestStage=%d BPState=%d Mapped=%d"),
            *CropActor->GetName(),
            GrowthIndex,
            ReadyToHarvestStage,
            bHasBPState ? static_cast<int32>(BPState) : -1,
            static_cast<int32>(MappedStage));

        return MappedStage;
    }

    if (!bHasBPState)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WildHarvestAdapter] GetCropState failed: no Growth Stage Index or Crop State on %s."),
            *CropActor->GetName());

        return ECropGrowthStage::Growing;
    }

    switch (BPState)
    {
    case 0:
        return ECropGrowthStage::BareSoil;

    case 1:
        return ECropGrowthStage::Growing;

    case 2:
        return ECropGrowthStage::Ready;

    case 3:
        return ECropGrowthStage::Harvested;

        // Your logs showed BPState=4 while visually mature / harvestable.
        // Treat as Ready instead of Dead for this pack.
    case 4:
        return ECropGrowthStage::Ready;

    default:
        return ECropGrowthStage::Growing;
    }
}

float UWildHarvestAdapter::GetCropWaterLevel(AActor* CropActor) const
{
    float Value = 0.0f;

    if (IsValid(CropActor))
    {
        ReadBPFloat(CropActor, BPVar_WaterLevel, Value);
    }

    if (!FMath::IsFinite(Value))
    {
        Value = 0.0f;
    }

    return FMath::Clamp(Value, 0.0f, 1.0f);
}

bool UWildHarvestAdapter::GetIsFertilized(AActor* CropActor) const
{
    bool Value = false;

    if (IsValid(CropActor))
    {
        ReadBPBool(CropActor, BPVar_IsFertilized, Value);
    }

    return Value;
}

bool UWildHarvestAdapter::GetIsDroughted(AActor* CropActor) const
{
    bool Value = false;

    if (IsValid(CropActor))
    {
        ReadBPBool(CropActor, BPVar_IsDroughted, Value);
    }

    return Value;
}

int32 UWildHarvestAdapter::GetGrowthStageIndex(AActor* CropActor) const
{
    int32 Value = INDEX_NONE;

    if (IsValid(CropActor))
    {
        ReadBPInt(CropActor, BPVar_GrowthStageIndex, Value);
    }

    return Value;
}

// ═══════════════════════════════════════════════════════════════════
// Yield Calculation
// ═══════════════════════════════════════════════════════════════════

int32 UWildHarvestAdapter::CalculateYield(
    FName CropRow,
    bool bFertilized,
    bool bDroughted) const
{
    int32 MinYield = 3;
    int32 MaxYield = 6;
    int32 FertilizerBonus = 2;
    int32 DroughtPenalty = 2;

    if (CropDataTable)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[WildHarvestAdapter] CalculateYield for '%s' using defaults."),
            *CropRow.ToString());
    }

    const int32 Base = FMath::RandRange(MinYield, MaxYield);

    int32 FinalYield = Base;

    if (bFertilized)
    {
        FinalYield += FertilizerBonus;
    }

    if (bDroughted)
    {
        FinalYield -= DroughtPenalty;
    }

    return FMath::Max(FinalYield, 1);
}

// ═══════════════════════════════════════════════════════════════════
// Item Tag Resolution
// ═══════════════════════════════════════════════════════════════════

FGameplayTag UWildHarvestAdapter::GetItemTagForCategory(ECropCategory Category) const
{
    switch (Category)
    {
    case ECropCategory::Grains:
        return TAG_Item_Grain;

    case ECropCategory::Berries:
        return TAG_Item_Berries;

    case ECropCategory::RootVegetables:
        return TAG_Item_RootVeg;

    case ECropCategory::SaladGarden:
        return TAG_Item_Greens;

    case ECropCategory::Herbs:
        return TAG_Item_Herbs;

    case ECropCategory::Mushrooms:
        return TAG_Item_Mushrooms;

    default:
        return TAG_Item_Food;
    }
}

ECropCategory UWildHarvestAdapter::GetCategoryForCropRow(FName CropRow) const
{
    return ClassifyCropRow(CropRow);
}

ECropCategory UWildHarvestAdapter::ClassifyCropRow(FName CropRow)
{
    const FString RowStr = CropRow.ToString();

    static const TArray<FString> Grains =
    {
        TEXT("LP_Barley"),
        TEXT("LP_Corn"),
        TEXT("LP_Oat"),
        TEXT("LP_Rice"),
        TEXT("LP_SugarCane"),
        TEXT("LP_Wheat")
    };

    for (const FString& G : Grains)
    {
        if (RowStr.Equals(G, ESearchCase::IgnoreCase))
        {
            return ECropCategory::Grains;
        }
    }

    static const TArray<FString> Berries =
    {
        TEXT("LP_Blueberry"),
        TEXT("LP_Cranberry"),
        TEXT("LP_Currant"),
        TEXT("LP_Gooseberry"),
        TEXT("LP_Raspberry"),
        TEXT("LP_Strawberry")
    };

    for (const FString& B : Berries)
    {
        if (RowStr.Equals(B, ESearchCase::IgnoreCase))
        {
            return ECropCategory::Berries;
        }
    }

    static const TArray<FString> HerbList =
    {
        TEXT("LP_Basil"),
        TEXT("LP_Chamomile"),
        TEXT("LP_Dill"),
        TEXT("LP_Echinacea"),
        TEXT("LP_Lavender"),
        TEXT("LP_Sage")
    };

    for (const FString& H : HerbList)
    {
        if (RowStr.Equals(H, ESearchCase::IgnoreCase))
        {
            return ECropCategory::Herbs;
        }
    }

    static const TArray<FString> Mushrooms =
    {
        TEXT("LP_BlackTrumpet"),
        TEXT("LP_Chanterelle"),
        TEXT("LP_Enoki"),
        TEXT("LP_FlyAgaric"),
        TEXT("LP_KingOyster"),
        TEXT("LP_TurkeyTail")
    };

    for (const FString& M : Mushrooms)
    {
        if (RowStr.Equals(M, ESearchCase::IgnoreCase))
        {
            return ECropCategory::Mushrooms;
        }
    }

    static const TArray<FString> RootVeg =
    {
        TEXT("LP_Carrot"),
        TEXT("LP_Ginger"),
        TEXT("LP_Onion"),
        TEXT("LP_Potato"),
        TEXT("LP_SweetPotato"),
        TEXT("LP_Turnip")
    };

    for (const FString& R : RootVeg)
    {
        if (RowStr.Equals(R, ESearchCase::IgnoreCase))
        {
            return ECropCategory::RootVegetables;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[WildHarvestAdapter] Unknown crop row '%s', defaulting to Grains"),
        *RowStr);

    return ECropCategory::Grains;
}

// ═══════════════════════════════════════════════════════════════════
// BP Variable Read Helpers
// ═══════════════════════════════════════════════════════════════════

bool UWildHarvestAdapter::ReadBPByte(
    AActor* Actor,
    FName VarName,
    uint8& OutVal) const
{
    if (!IsValid(Actor))
    {
        return false;
    }

    FByteProperty* Prop = CastField<FByteProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (Prop)
    {
        OutVal = *Prop->ContainerPtrToValuePtr<uint8>(Actor);
        return true;
    }

    FEnumProperty* EnumProp = CastField<FEnumProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (EnumProp)
    {
        FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();

        if (UnderlyingProp)
        {
            OutVal = static_cast<uint8>(
                UnderlyingProp->GetSignedIntPropertyValue(
                    EnumProp->ContainerPtrToValuePtr<void>(Actor)));

            return true;
        }
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("[WildHarvestAdapter] ReadBPByte: Property '%s' not found on %s"),
        *VarName.ToString(),
        *Actor->GetName());

    return false;
}

bool UWildHarvestAdapter::ReadBPFloat(
    AActor* Actor,
    FName VarName,
    float& OutVal) const
{
    if (!IsValid(Actor))
    {
        return false;
    }

    FFloatProperty* FloatProp = CastField<FFloatProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (FloatProp)
    {
        OutVal = FloatProp->GetPropertyValue(
            FloatProp->ContainerPtrToValuePtr<void>(Actor));

        return true;
    }

    FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (DoubleProp)
    {
        OutVal = static_cast<float>(DoubleProp->GetPropertyValue(
            DoubleProp->ContainerPtrToValuePtr<void>(Actor)));

        return true;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("[WildHarvestAdapter] ReadBPFloat: Property '%s' not found on %s"),
        *VarName.ToString(),
        *Actor->GetName());

    return false;
}

bool UWildHarvestAdapter::ReadBPBool(
    AActor* Actor,
    FName VarName,
    bool& OutVal) const
{
    if (!IsValid(Actor))
    {
        return false;
    }

    FBoolProperty* Prop = CastField<FBoolProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (Prop)
    {
        OutVal = Prop->GetPropertyValue(
            Prop->ContainerPtrToValuePtr<void>(Actor));

        return true;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("[WildHarvestAdapter] ReadBPBool: Property '%s' not found on %s"),
        *VarName.ToString(),
        *Actor->GetName());

    return false;
}

bool UWildHarvestAdapter::ReadBPInt(
    AActor* Actor,
    FName VarName,
    int32& OutVal) const
{
    if (!IsValid(Actor))
    {
        return false;
    }

    FIntProperty* Prop = CastField<FIntProperty>(
        Actor->GetClass()->FindPropertyByName(VarName));

    if (Prop)
    {
        OutVal = Prop->GetPropertyValue(
            Prop->ContainerPtrToValuePtr<void>(Actor));

        return true;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("[WildHarvestAdapter] ReadBPInt: Property '%s' not found on %s"),
        *VarName.ToString(),
        *Actor->GetName());

    return false;
}
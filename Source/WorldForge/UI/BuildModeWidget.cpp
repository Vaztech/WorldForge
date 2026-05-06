#include "BuildModeWidget.h"
#include "Player/BuildModeManager.h"
#include "WorldForgeDataRegistry.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UBuildModeManager* UBuildModeWidget::GetManager()
{
	if (CachedManager)
	{
		return CachedManager;
	}

	UWorld* W = GetWorld();
	if (W)
	{
		CachedManager = W->GetSubsystem<UBuildModeManager>();
	}

	return CachedManager;
}

UWorldForgeDataRegistry* UBuildModeWidget::GetRegistry() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UWorldForgeDataRegistry>();
	}

	return nullptr;
}

void UBuildModeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->OnBuildModeChanged.AddDynamic(this, &UBuildModeWidget::OnBuildModeChanged);
	Mgr->OnCellCountChanged.AddDynamic(this, &UBuildModeWidget::OnCellCountChanged);
	Mgr->OnHoveredCellChanged.AddDynamic(this, &UBuildModeWidget::OnHoveredCellChanged);
	Mgr->OnBuildSelectionChanged.AddDynamic(this, &UBuildModeWidget::OnSelectionChanged);
	Mgr->OnBuildFeedbackMessage.AddDynamic(this, &UBuildModeWidget::OnBuildFeedbackMessage);

	// Push initial state so the widget has valid data immediately on show.
	OnBuildModeChanged(Mgr->GetCurrentMode());
	OnCellCountChanged(Mgr->GetSelectedCellCount());
	OnSelectionChanged(
		Mgr->GetCurrentMode(),
		Mgr->GetSelectedBuildingTag(),
		Mgr->GetSelectedMaterialTag()
	);

	if (Mgr->HasValidHoveredCell())
	{
		OnHoveredCellChanged(Mgr->GetHoveredCell());
	}

	// Push an initial stockpile snapshot for whichever selection is currently active.
	OnStockpileChanged(Mgr->GetCurrentRequiredItemTag(), Mgr->GetCurrentAvailableUnits());

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Constructed and bound to BuildModeManager."));
}

void UBuildModeWidget::NativeDestruct()
{
	UBuildModeManager* Mgr = GetManager();
	if (Mgr)
	{
		Mgr->OnBuildModeChanged.RemoveDynamic(this, &UBuildModeWidget::OnBuildModeChanged);
		Mgr->OnCellCountChanged.RemoveDynamic(this, &UBuildModeWidget::OnCellCountChanged);
		Mgr->OnHoveredCellChanged.RemoveDynamic(this, &UBuildModeWidget::OnHoveredCellChanged);
		Mgr->OnBuildSelectionChanged.RemoveDynamic(this, &UBuildModeWidget::OnSelectionChanged);
		Mgr->OnBuildFeedbackMessage.RemoveDynamic(this, &UBuildModeWidget::OnBuildFeedbackMessage);
	}

	CachedManager = nullptr;

	Super::NativeDestruct();
}

// ================================
// Actions (called from Blueprint buttons)
// ================================

void UBuildModeWidget::SelectPrefabBuilding(FGameplayTag BuildingTag)
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->EnterPrefabMode(BuildingTag);

	OnStockpileChanged(Mgr->GetCurrentRequiredItemTag(), Mgr->GetCurrentAvailableUnits());

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Prefab selected: %s"), *BuildingTag.ToString());
}

void UBuildModeWidget::SelectMaterial(FGameplayTag MaterialTag)
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	if (Mgr->GetCurrentMode() == EBuildMode::CustomDraw)
	{
		Mgr->ExitBuildMode();
	}

	Mgr->EnterCustomDrawMode(MaterialTag);

	OnStockpileChanged(Mgr->GetCurrentRequiredItemTag(), Mgr->GetCurrentAvailableUnits());

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Material selected: %s"), *MaterialTag.ToString());
}

void UBuildModeWidget::SwitchToCustomDrawMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	FGameplayTag DefaultMaterial = FGameplayTag::RequestGameplayTag(
		FName(TEXT("WorldForge.Voxel.Material.Stone")));

	Mgr->EnterCustomDrawMode(DefaultMaterial);

	OnStockpileChanged(Mgr->GetCurrentRequiredItemTag(), Mgr->GetCurrentAvailableUnits());

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to custom draw mode."));
}


void UBuildModeWidget::SwitchToDigMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->EnterDigMode();

	// Dig has no material/building cost in Phase 1.
	OnStockpileChanged(FGameplayTag::EmptyTag, 0);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to dig mode."));
}

void UBuildModeWidget::SwitchToMineMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->EnterMineMode();

	OnStockpileChanged(FGameplayTag::EmptyTag, 0);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to mine mode."));
}

void UBuildModeWidget::SwitchToFarmMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->EnterFarmMode();

	OnStockpileChanged(FGameplayTag::EmptyTag, 0);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to farm mode."));
}

void UBuildModeWidget::CancelBuild()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->HandleCancel();
}

void UBuildModeWidget::ConfirmBuild()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->HandleConfirm();
}

// ================================
// Read-only data accessors for Blueprint UI
// ================================

TArray<FGameplayTag> UBuildModeWidget::GetAvailableBuildingTags() const
{
	TArray<FGameplayTag> Result;

	if (const UWorldForgeDataRegistry* Registry = GetRegistry())
	{
		const TMap<FGameplayTag, UBuildingDefinition*>& Buildings = Registry->GetAllBuildings();

		for (const TPair<FGameplayTag, UBuildingDefinition*>& Pair : Buildings)
		{
			if (Pair.Key.IsValid() && Pair.Value)
			{
				Result.Add(Pair.Key);
			}
		}
	}

	return Result;
}

TArray<FGameplayTag> UBuildModeWidget::GetAvailableMaterialTags() const
{
	TArray<FGameplayTag> Result;

	Result.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone"))));
	Result.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Wood"))));

	return Result;
}

FText UBuildModeWidget::GetBuildingDisplayName(FGameplayTag BuildingTag) const
{
	if (!BuildingTag.IsValid())
	{
		return FText::FromString(TEXT("None"));
	}

	if (const UWorldForgeDataRegistry* Registry = GetRegistry())
	{
		if (const UBuildingDefinition* Def = Registry->FindBuilding(BuildingTag))
		{
			if (!Def->DisplayName.IsEmpty())
			{
				return Def->DisplayName;
			}
		}
	}

	return FText::FromString(BuildingTag.ToString());
}

FText UBuildModeWidget::GetItemDisplayName(FGameplayTag ItemTag) const
{
	if (!ItemTag.IsValid())
	{
		return FText::FromString(TEXT("None"));
	}

	if (const UWorldForgeDataRegistry* Registry = GetRegistry())
	{
		if (const UItemDefinition* Def = Registry->FindItem(ItemTag))
		{
			if (!Def->DisplayName.IsEmpty())
			{
				return Def->DisplayName;
			}
		}
	}

	return FText::FromString(ItemTag.ToString());
}

FText UBuildModeWidget::GetMaterialDisplayName(FGameplayTag MaterialTag) const
{
	if (!MaterialTag.IsValid())
	{
		return FText::FromString(TEXT("None"));
	}

	// If you later add voxel-material registry assets, resolve them here.
	return FText::FromString(MaterialTag.ToString());
}

FText UBuildModeWidget::GetSelectedBuildingDisplayName() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr
		? GetBuildingDisplayName(Mgr->GetSelectedBuildingTag())
		: FText::FromString(TEXT("None"));
}

FText UBuildModeWidget::GetSelectedMaterialDisplayName() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr
		? GetMaterialDisplayName(Mgr->GetSelectedMaterialTag())
		: FText::FromString(TEXT("None"));
}

FText UBuildModeWidget::GetCurrentCostLabel() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	if (!Mgr)
	{
		return FText::FromString(TEXT("Cost: N/A"));
	}

	if (Mgr->GetCurrentToolMode() == EBuildToolMode::Dig ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Mine ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Farm)
	{
		return FText::FromString(TEXT("Cost: None"));
	}

	const int32 RequiredUnits = Mgr->GetCurrentRequiredUnits();
	const FGameplayTag RequiredItemTag = Mgr->GetCurrentRequiredItemTag();

	if (!RequiredItemTag.IsValid() || RequiredUnits <= 0)
	{
		return FText::FromString(TEXT("Cost: N/A"));
	}

	const FText ItemName = GetItemDisplayName(RequiredItemTag);

	return FText::Format(
		NSLOCTEXT("WorldForge", "BuildMode_CurrentCost", "Cost: {0} x {1}"),
		FText::AsNumber(RequiredUnits),
		ItemName
	);
}

FText UBuildModeWidget::GetCurrentAvailabilityLabel() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	if (!Mgr)
	{
		return FText::FromString(TEXT("Available: N/A"));
	}

	if (Mgr->GetCurrentToolMode() == EBuildToolMode::Dig ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Mine ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Farm)
	{
		return FText::FromString(TEXT("Available: N/A"));
	}

	const int32 AvailableUnits = Mgr->GetCurrentAvailableUnits();
	const int32 RequiredUnits = Mgr->GetCurrentRequiredUnits();

	return FText::Format(
		NSLOCTEXT("WorldForge", "BuildMode_CurrentAvailability", "Available: {0} / Required: {1}"),
		FText::AsNumber(AvailableUnits),
		FText::AsNumber(RequiredUnits)
	);
}

bool UBuildModeWidget::GetCanAffordCurrentSelection() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	if (!Mgr)
	{
		return false;
	}

	if (Mgr->GetCurrentToolMode() == EBuildToolMode::Dig ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Mine ||
		Mgr->GetCurrentToolMode() == EBuildToolMode::Farm)
	{
		return true;
	}

	return Mgr->CanAffordCurrentSelection();
}

int32 UBuildModeWidget::GetCurrentCellCount() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetSelectedCellCount() : 0;
}

EBuildToolMode UBuildModeWidget::GetCurrentToolMode() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetCurrentToolMode() : EBuildToolMode::None;
}

FText UBuildModeWidget::GetCurrentToolModeLabel() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	if (!Mgr)
	{
		return FText::FromString(TEXT("Mode: None"));
	}

	switch (Mgr->GetCurrentToolMode())
	{
	case EBuildToolMode::Place:
		switch (Mgr->GetCurrentMode())
		{
		case EBuildMode::Prefab:
			return FText::FromString(TEXT("Mode: Prefab"));
		case EBuildMode::CustomDraw:
			return FText::FromString(TEXT("Mode: Free Build"));
		default:
			return FText::FromString(TEXT("Mode: Place"));
		}

	case EBuildToolMode::Remove:
		return FText::FromString(TEXT("Mode: Remove"));

	case EBuildToolMode::Dig:
		return FText::FromString(TEXT("Mode: Dig"));

	case EBuildToolMode::Mine:
		return FText::FromString(TEXT("Mode: Mine"));

	case EBuildToolMode::Farm:
		return FText::FromString(TEXT("Mode: Farm"));

	case EBuildToolMode::None:
	default:
		return FText::FromString(TEXT("Mode: None"));
	}
}

bool UBuildModeWidget::IsDigModeActive() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr && Mgr->GetCurrentToolMode() == EBuildToolMode::Dig;
}

bool UBuildModeWidget::IsMineModeActive() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr && Mgr->GetCurrentToolMode() == EBuildToolMode::Mine;
}

bool UBuildModeWidget::IsFarmModeActive() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr && Mgr->GetCurrentToolMode() == EBuildToolMode::Farm;
}
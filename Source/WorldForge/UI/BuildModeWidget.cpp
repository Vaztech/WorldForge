// Source/WorldForge/UI/BuildModeWidget.cpp
#include "BuildModeWidget.h"
#include "Player/BuildModeManager.h"
#include "Engine/World.h"

UBuildModeManager* UBuildModeWidget::GetManager()
{
	if (CachedManager) return CachedManager;

	UWorld* W = GetWorld();
	if (W)
	{
		CachedManager = W->GetSubsystem<UBuildModeManager>();
	}

	return CachedManager;
}

void UBuildModeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	Mgr->OnBuildModeChanged.AddDynamic(this, &UBuildModeWidget::OnBuildModeChanged);
	Mgr->OnCellCountChanged.AddDynamic(this, &UBuildModeWidget::OnCellCountChanged);
	Mgr->OnHoveredCellChanged.AddDynamic(this, &UBuildModeWidget::OnHoveredCellChanged);
	Mgr->OnBuildSelectionChanged.AddDynamic(this, &UBuildModeWidget::OnSelectionChanged);
	Mgr->OnBuildFeedbackMessage.AddDynamic(this, &UBuildModeWidget::OnBuildFeedbackMessage);

	// Push initial state so the widget has valid data immediately on show.
	OnBuildModeChanged(Mgr->GetCurrentMode());
	OnCellCountChanged(Mgr->GetSelectedCellCount());
	OnSelectionChanged(Mgr->GetCurrentMode(), Mgr->GetSelectedBuildingTag(), Mgr->GetSelectedMaterialTag());

	if (Mgr->HasValidHoveredCell())
	{
		OnHoveredCellChanged(Mgr->GetHoveredCell());
	}

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
	if (!Mgr) return;

	Mgr->EnterPrefabMode(BuildingTag);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Prefab selected: %s"), *BuildingTag.ToString());
}

void UBuildModeWidget::SelectMaterial(FGameplayTag MaterialTag)
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	if (Mgr->GetCurrentMode() == EBuildMode::CustomDraw)
	{
		Mgr->ExitBuildMode();
	}

	Mgr->EnterCustomDrawMode(MaterialTag);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Material selected: %s"), *MaterialTag.ToString());
}

void UBuildModeWidget::SwitchToCustomDrawMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	FGameplayTag DefaultMaterial = FGameplayTag::RequestGameplayTag(
		FName(TEXT("WorldForge.Voxel.Material.Stone")));

	Mgr->EnterCustomDrawMode(DefaultMaterial);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to custom draw mode."));
}

void UBuildModeWidget::CancelBuild()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	Mgr->HandleCancel();
}

void UBuildModeWidget::ConfirmBuild()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr) return;

	Mgr->HandleConfirm();
}
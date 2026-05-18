#include "BuildModeWidget.h"
#include "Player/BuildModeManager.h"
#include "WorldForgeDataRegistry.h"
#include "Build/WorldForgeBuildPrefabDataSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Player/WorldForgePlayerController.h"

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

UWorldForgeBuildPrefabDataSubsystem* UBuildModeWidget::GetBuildDataSubsystem() const
{
	if (UWorld* W = GetWorld())
	{
		return W->GetSubsystem<UWorldForgeBuildPrefabDataSubsystem>();
	}

	return nullptr;
}

namespace
{
	constexpr int32 WF_BuildDataLogSampleCount = 5;

	static void WF_ExitMineModeBeforeNonMineTool(UBuildModeWidget* Widget)
	{
		if (!Widget)
		{
			return;
		}

		if (AWorldForgePlayerController* PC = Widget->GetOwningPlayer<AWorldForgePlayerController>())
		{
			PC->ExecuteExitMineModeIfActive();
		}
	}

	static FString WF_NameToString(FName Value)
	{
		return Value.IsNone() ? FString(TEXT("None")) : Value.ToString();
	}

	static FString WF_TextOrName(const FText& TextValue, FName FallbackName)
	{
		return TextValue.IsEmpty() ? WF_NameToString(FallbackName) : TextValue.ToString();
	}

	static FString WF_PathOrNone(const FString& Path)
	{
		return Path.IsEmpty() ? FString(TEXT("None")) : Path;
	}

	static FString WF_NormalizeObjectPath(const FString& AssetPath)
	{
		FString TrimmedPath = AssetPath;
		TrimmedPath.TrimStartAndEndInline();

		if (TrimmedPath.IsEmpty() || TrimmedPath.Contains(TEXT("'")))
		{
			return TrimmedPath;
		}

		const int32 LastSlashIndex = TrimmedPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		const FString AssetName = LastSlashIndex == INDEX_NONE
			? TrimmedPath
			: TrimmedPath.Mid(LastSlashIndex + 1);

		if (AssetName.IsEmpty() || AssetName.Contains(TEXT(".")))
		{
			return TrimmedPath;
		}

		return FString::Printf(TEXT("%s.%s"), *TrimmedPath, *AssetName);
	}

	static bool WF_LoadPreviewStaticMesh(
		const FString& MeshPath,
		const TCHAR* SelectionLabel,
		const TCHAR* IDFieldName,
		FName RowID,
		const TCHAR* PathFieldName,
		UStaticMesh*& OutMesh)
	{
		OutMesh = nullptr;

		if (MeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BuildModeWidget] %s preview mesh unavailable: %s=%s has empty %s."),
				SelectionLabel,
				IDFieldName,
				*WF_NameToString(RowID),
				PathFieldName);
			return false;
		}

		const FString ObjectPath = WF_NormalizeObjectPath(MeshPath);
		OutMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
		if (!OutMesh)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BuildModeWidget] %s preview mesh load failed: %s=%s %s=%s ObjectPath=%s."),
				SelectionLabel,
				IDFieldName,
				*WF_NameToString(RowID),
				PathFieldName,
				*MeshPath,
				*ObjectPath);
			return false;
		}

		return true;
	}

	static void WF_PushResolvedPreviewMeshToManager(
		UBuildModeManager* Manager,
		UStaticMesh* PreviewMesh,
		const TCHAR* SelectionLabel,
		const TCHAR* IDFieldName,
		FName RowID,
		const FString& MeshPath,
		bool bIsPrefab)
	{
		if (!PreviewMesh)
		{
			return;
		}

		if (!Manager)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BuildModeWidget] %s preview mesh resolved but BuildModeManager is unavailable: %s=%s MeshPath=%s Mesh=%s."),
				SelectionLabel,
				IDFieldName,
				*WF_NameToString(RowID),
				*MeshPath,
				*PreviewMesh->GetPathName());
			return;
		}

		const bool bPreviewStarted = Manager->BeginDataTablePreviewMode(PreviewMesh, RowID, bIsPrefab);
		if (bPreviewStarted)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[BuildModeWidget] %s DataTable preview mode activated: %s=%s MeshPath=%s Mesh=%s."),
				SelectionLabel,
				IDFieldName,
				*WF_NameToString(RowID),
				*MeshPath,
				*PreviewMesh->GetPathName());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BuildModeWidget] %s preview mesh resolved but DataTable preview mode could not be activated: %s=%s MeshPath=%s Mesh=%s."),
				SelectionLabel,
				IDFieldName,
				*WF_NameToString(RowID),
				*MeshPath,
				*PreviewMesh->GetPathName());
		}
	}

	static FString WF_FormatCategorySummary(const FWorldForgeBuildCategoryRuleRow& Row)
	{
		const FString CategoryID = WF_NameToString(Row.CategoryID);
		const FString DisplayName = WF_TextOrName(Row.DisplayName, Row.CategoryID);

		return FString::Printf(TEXT("%s | %s"), *CategoryID, *DisplayName);
	}

	static FString WF_FormatBuildPieceSummary(const FWorldForgeBuildPieceRow& Row)
	{
		const FString BuildID = WF_NameToString(Row.BuildID);
		const FString DisplayName = WF_TextOrName(Row.DisplayName, Row.BuildID);
		const FString Category = WF_NameToString(Row.Category);
		const FString SubCategory = WF_NameToString(Row.SubCategory);

		return FString::Printf(
			TEXT("%s | %s | %s/%s"),
			*BuildID,
			*DisplayName,
			*Category,
			*SubCategory);
	}

	static FString WF_FormatPrefabSummary(const FWorldForgeBuildingPrefabRow& Row)
	{
		const FString PrefabID = WF_NameToString(Row.PrefabID);
		const FString DisplayName = WF_TextOrName(Row.DisplayName, Row.PrefabID);
		const FString Category = WF_NameToString(Row.Category);
		const FString SubCategory = WF_NameToString(Row.SubCategory);

		return FString::Printf(
			TEXT("%s | %s | %s/%s"),
			*PrefabID,
			*DisplayName,
			*Category,
			*SubCategory);
	}
}

void UBuildModeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildCompactRuntimeLayout();

	LogBuildPrefabDataTableSummary();

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
	Mgr->OnBuildFeedbackMessage.AddDynamic(this, &UBuildModeWidget::HandleNativeBuildFeedbackMessage);

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
		Mgr->OnBuildFeedbackMessage.RemoveDynamic(this, &UBuildModeWidget::HandleNativeBuildFeedbackMessage);
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

	WF_ExitMineModeBeforeNonMineTool(this);

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

	WF_ExitMineModeBeforeNonMineTool(this);

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

	WF_ExitMineModeBeforeNonMineTool(this);

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

	WF_ExitMineModeBeforeNonMineTool(this);

	Mgr->EnterDigMode();

	// Dig has no material/building cost in Phase 1.
	OnStockpileChanged(FGameplayTag::EmptyTag, 0);

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Switched to dig mode."));
}

void UBuildModeWidget::SwitchToMineMode()
{
	AWorldForgePlayerController* PC = GetOwningPlayer<AWorldForgePlayerController>();
	if (PC)
	{
		PC->ExecuteToggleMineMode();
		OnStockpileChanged(FGameplayTag::EmptyTag, 0);
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Toggled mine mode via player controller."));
		return;
	}

	// Fallback only. This enters Mine mode but cannot activate controller-owned
	// camera/cutaway subsystems, so the player controller path above is preferred.
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->EnterMineMode();
	OnStockpileChanged(FGameplayTag::EmptyTag, 0);
	UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] Switched to mine mode through manager fallback."));
}

void UBuildModeWidget::SwitchToFarmMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	WF_ExitMineModeBeforeNonMineTool(this);

	// Safety guard: if the Farm button is clicked while Farm mode already has
	// painted cells, commit the farm selection instead of toggling/canceling it.
	// This protects Blueprint wiring where the same button may be reused as a
	// confirm-style action after painting.
	if (Mgr->GetCurrentToolMode() == EBuildToolMode::Farm && Mgr->GetSelectedCellCount() > 0)
	{
		Mgr->HandleConfirm();
		OnStockpileChanged(FGameplayTag::EmptyTag, 0);
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Farm mode already active; confirming farm selection."));
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

	// Mine mode owns extra controller/camera/cutaway state. Exit through the
	// player controller so TerrainCutawaySubsystem and mining camera state are
	// properly cleaned up.
	if (Mgr->IsInMineMode())
	{
		if (AWorldForgePlayerController* PC = GetOwningPlayer<AWorldForgePlayerController>())
		{
			PC->ExecuteToggleMineMode();
			return;
		}
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

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] ConfirmBuild pressed. ToolMode=%d CellCount=%d"),
		static_cast<int32>(Mgr->GetCurrentToolMode()),
		Mgr->GetSelectedCellCount());

	Mgr->HandleConfirm();
}


void UBuildModeWidget::ToggleStaticGridOverlay()
{
	if (UBuildModeManager* Mgr = GetManager())
	{
		const bool bEnabled = Mgr->ToggleStaticBaseGrid();
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Static grid overlay toggled: %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
	}
}

void UBuildModeWidget::SetStaticGridOverlayEnabled(bool bEnabled)
{
	if (UBuildModeManager* Mgr = GetManager())
	{
		Mgr->SetStaticBaseGridEnabled(bEnabled);
		UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Static grid overlay set: %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
	}
}

bool UBuildModeWidget::IsStaticGridOverlayEnabled() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr && Mgr->IsStaticBaseGridEnabled();
}

void UBuildModeWidget::ToggleMiningRemovalMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->ToggleMiningRemovalMode();
	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Toggled mining removal mode."));
}

void UBuildModeWidget::SetMiningRemovalMode(EMiningRemovalMode NewMode)
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->SetMiningRemovalMode(NewMode);
	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Set mining removal mode: %d"), static_cast<int32>(NewMode));
}

EMiningRemovalMode UBuildModeWidget::GetMiningRemovalMode() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetMiningRemovalMode() : EMiningRemovalMode::VoxelCube;
}

FText UBuildModeWidget::GetMiningRemovalModeLabel() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetMiningRemovalModeText() : FText::FromString(TEXT("Mining: Voxel Cube"));
}


void UBuildModeWidget::ToggleMiningHeightMode()
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	const EWFMiningHeightMode NewMode = Mgr->ToggleMiningHeightMode();
	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Toggled mining height mode: %d"), static_cast<int32>(NewMode));
}

void UBuildModeWidget::SetMiningHeightMode(EWFMiningHeightMode NewMode)
{
	UBuildModeManager* Mgr = GetManager();
	if (!Mgr)
	{
		return;
	}

	Mgr->SetMiningHeightMode(NewMode);
	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] Set mining height mode: %d"), static_cast<int32>(NewMode));
}

EWFMiningHeightMode UBuildModeWidget::GetMiningHeightMode() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetMiningHeightMode() : EWFMiningHeightMode::TwoVoxelColumn;
}

FText UBuildModeWidget::GetMiningHeightModeLabel() const
{
	const UBuildModeManager* Mgr = const_cast<UBuildModeWidget*>(this)->GetManager();
	return Mgr ? Mgr->GetMiningHeightModeText() : FText::FromString(TEXT("Mine Height: 2 Block Column"));
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

TArray<FString> UBuildModeWidget::GetBuildDataCategorySummaries() const
{
	TArray<FString> Result;

	if (const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem())
	{
		TArray<FWorldForgeBuildCategoryRuleRow> CategoryRows;
		DataSubsystem->GetAllBuildCategoryRules(CategoryRows);

		Result.Reserve(CategoryRows.Num());
		for (const FWorldForgeBuildCategoryRuleRow& Row : CategoryRows)
		{
			Result.Add(WF_FormatCategorySummary(Row));
		}
	}

	Result.Sort();
	return Result;
}

TArray<FString> UBuildModeWidget::GetBuildDataBuildPieceSummaries(FName Category) const
{
	TArray<FString> Result;

	if (const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem())
	{
		TArray<FWorldForgeBuildPieceRow> PieceRows;
		if (Category.IsNone())
		{
			DataSubsystem->GetAllBuildPieces(PieceRows);
		}
		else
		{
			DataSubsystem->GetBuildPiecesByCategory(Category, PieceRows);
		}

		Result.Reserve(PieceRows.Num());
		for (const FWorldForgeBuildPieceRow& Row : PieceRows)
		{
			// Skip rows that have no mesh and no preview mesh — they cannot preview or place.
			if (Row.MeshPath.IsEmpty() && Row.PreviewMeshPath.IsEmpty())
			{
				UE_LOG(LogTemp, Log,
					TEXT("[BuildModeWidget] BuildPiece row skipped (no mesh): BuildID=%s DisplayName=%s"),
					*Row.BuildID.ToString(),
					*Row.DisplayName.ToString());
				continue;
			}

			Result.Add(WF_FormatBuildPieceSummary(Row));
		}
	}

	Result.Sort();
	return Result;
}

TArray<FString> UBuildModeWidget::GetBuildDataPrefabSummaries(FName Category) const
{
	TArray<FString> Result;

	if (const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem())
	{
		TArray<FWorldForgeBuildingPrefabRow> PrefabRows;
		if (Category.IsNone())
		{
			DataSubsystem->GetAllBuildingPrefabs(PrefabRows);
		}
		else
		{
			DataSubsystem->GetBuildingPrefabsByCategory(Category, PrefabRows);
		}

		Result.Reserve(PrefabRows.Num());
		for (const FWorldForgeBuildingPrefabRow& Row : PrefabRows)
		{
			Result.Add(WF_FormatPrefabSummary(Row));
		}
	}

	Result.Sort();
	return Result;
}

void UBuildModeWidget::LogBuildPrefabDataTableSummary() const
{
	const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem();
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] Build prefab data subsystem unavailable for UI."));
		return;
	}

	TArray<FWorldForgeBuildCategoryRuleRow> CategoryRows;
	TArray<FWorldForgeBuildPieceRow> PieceRows;
	TArray<FWorldForgeBuildingPrefabRow> PrefabRows;

	DataSubsystem->GetAllBuildCategoryRules(CategoryRows);
	DataSubsystem->GetAllBuildPieces(PieceRows);
	DataSubsystem->GetAllBuildingPrefabs(PrefabRows);

	UE_LOG(LogTemp, Log,
		TEXT("[BuildModeWidget] DataTable rows exposed to build UI. Categories=%d BuildPieces=%d Prefabs=%d"),
		CategoryRows.Num(),
		PieceRows.Num(),
		PrefabRows.Num());

	for (int32 Index = 0; Index < FMath::Min(CategoryRows.Num(), WF_BuildDataLogSampleCount); ++Index)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[BuildModeWidget] UI Category[%d] %s"),
			Index,
			*WF_FormatCategorySummary(CategoryRows[Index]));
	}

	for (int32 Index = 0; Index < FMath::Min(PieceRows.Num(), WF_BuildDataLogSampleCount); ++Index)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[BuildModeWidget] UI BuildPiece[%d] %s"),
			Index,
			*WF_FormatBuildPieceSummary(PieceRows[Index]));
	}

	for (int32 Index = 0; Index < FMath::Min(PrefabRows.Num(), WF_BuildDataLogSampleCount); ++Index)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[BuildModeWidget] UI Prefab[%d] %s"),
			Index,
			*WF_FormatPrefabSummary(PrefabRows[Index]));
	}
}

void UBuildModeWidget::LogSelectedBuildPieceID(FName BuildID) const
{
	if (BuildID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] DataTable build piece selection ignored: BuildID is None."));
		return;
	}

	if (const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem())
	{
		FWorldForgeBuildPieceRow Row;
		if (DataSubsystem->GetBuildPieceByID(BuildID, Row))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[BuildModeWidget] DataTable build piece selected for UI only: %s"),
				*WF_FormatBuildPieceSummary(Row));
			return;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BuildModeWidget] DataTable build piece selection ignored: %s was not found."),
		*BuildID.ToString());
}

void UBuildModeWidget::LogSelectedBuildPrefabID(FName PrefabID) const
{
	if (PrefabID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] DataTable prefab selection ignored: PrefabID is None."));
		return;
	}

	if (const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem())
	{
		FWorldForgeBuildingPrefabRow Row;
		if (DataSubsystem->GetBuildingPrefabByID(PrefabID, Row))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[BuildModeWidget] DataTable prefab selected for UI only: %s"),
				*WF_FormatPrefabSummary(Row));
			return;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BuildModeWidget] DataTable prefab selection ignored: %s was not found."),
		*PrefabID.ToString());
}

void UBuildModeWidget::SelectBuildPieceFromDataTable(FName BuildID)
{
	if (BuildID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] DataTable build piece selection ignored: BuildID is None."));
		return;
	}

	const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem();
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] DataTable build piece selection ignored: build prefab data subsystem unavailable."));
		return;
	}

	if (!DataSubsystem->IsBuildPieceValid(BuildID))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] DataTable build piece selection ignored: %s is not a valid BuildID."),
			*BuildID.ToString());
		return;
	}

	WF_ExitMineModeBeforeNonMineTool(this);

	SelectedBuildPieceID = BuildID;
	bHasSelectedBuildPiece = true;
	SelectedBuildingPrefabID = NAME_None;
	bHasSelectedBuildingPrefab = false;

	FWorldForgeBuildPieceRow Row;
	if (DataSubsystem->GetBuildPieceByID(BuildID, Row))
	{
		const FString PreviewMeshPath = Row.PreviewMeshPath.IsEmpty() ? Row.MeshPath : Row.PreviewMeshPath;
		const TCHAR* PreviewMeshFieldName = Row.PreviewMeshPath.IsEmpty()
			? TEXT("MeshPath")
			: TEXT("PreviewMeshPath");

		UE_LOG(LogTemp, Log,
			TEXT("[BuildModeWidget] DataTable build piece selected: BuildID=%s DisplayName=%s Category=%s MeshPath=%s PreviewMeshPath=%s"),
			*BuildID.ToString(),
			*WF_TextOrName(Row.DisplayName, BuildID),
			*WF_NameToString(Row.Category),
			*WF_PathOrNone(Row.MeshPath),
			*WF_PathOrNone(Row.PreviewMeshPath));

		UStaticMesh* PreviewMesh = nullptr;
		if (WF_LoadPreviewStaticMesh(
			PreviewMeshPath,
			TEXT("Build piece"),
			TEXT("BuildID"),
			BuildID,
			PreviewMeshFieldName,
			PreviewMesh))
		{
			WF_PushResolvedPreviewMeshToManager(
				GetManager(),
				PreviewMesh,
				TEXT("Build piece"),
				TEXT("BuildID"),
				BuildID,
				PreviewMeshPath,
				false);
		}
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BuildModeWidget] DataTable build piece selected: BuildID=%s"),
		*BuildID.ToString());
}

void UBuildModeWidget::SelectBuildingPrefabFromDataTable(FName PrefabID)
{
	if (PrefabID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeWidget] DataTable prefab selection ignored: PrefabID is None."));
		return;
	}

	const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem();
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] DataTable prefab selection ignored: build prefab data subsystem unavailable."));
		return;
	}

	if (!DataSubsystem->IsBuildingPrefabValid(PrefabID))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] DataTable prefab selection ignored: %s is not a valid PrefabID."),
			*PrefabID.ToString());
		return;
	}

	WF_ExitMineModeBeforeNonMineTool(this);

	SelectedBuildingPrefabID = PrefabID;
	bHasSelectedBuildingPrefab = true;
	SelectedBuildPieceID = NAME_None;
	bHasSelectedBuildPiece = false;

	FWorldForgeBuildingPrefabRow Row;
	if (DataSubsystem->GetBuildingPrefabByID(PrefabID, Row))
	{
		const FString PreviewMeshPath = Row.PrimaryMeshPath.IsEmpty() ? Row.PreviewMeshPath : Row.PrimaryMeshPath;
		const TCHAR* PreviewMeshFieldName = Row.PrimaryMeshPath.IsEmpty()
			? TEXT("PreviewMeshPath")
			: TEXT("PrimaryMeshPath");

		UE_LOG(LogTemp, Log,
			TEXT("[BuildModeWidget] DataTable prefab selected: PrefabID=%s DisplayName=%s Category=%s PrimaryMeshPath=%s PreviewMeshPath=%s BlueprintClassPath=%s"),
			*PrefabID.ToString(),
			*WF_TextOrName(Row.DisplayName, PrefabID),
			*WF_NameToString(Row.Category),
			*WF_PathOrNone(Row.PrimaryMeshPath),
			*WF_PathOrNone(Row.PreviewMeshPath),
			*WF_PathOrNone(Row.BlueprintClassPath));

		UStaticMesh* PreviewMesh = nullptr;
		if (WF_LoadPreviewStaticMesh(
			PreviewMeshPath,
			TEXT("Building prefab"),
			TEXT("PrefabID"),
			PrefabID,
			PreviewMeshFieldName,
			PreviewMesh))
		{
			WF_PushResolvedPreviewMeshToManager(
				GetManager(),
				PreviewMesh,
				TEXT("Building prefab"),
				TEXT("PrefabID"),
				PrefabID,
				PreviewMeshPath,
				true);
		}
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BuildModeWidget] DataTable prefab selected: PrefabID=%s"),
		*PrefabID.ToString());
}

void UBuildModeWidget::ClearDataTableBuildSelection()
{
	SelectedBuildPieceID = NAME_None;
	SelectedBuildingPrefabID = NAME_None;
	bHasSelectedBuildPiece = false;
	bHasSelectedBuildingPrefab = false;

	UE_LOG(LogTemp, Log, TEXT("[BuildModeWidget] DataTable build selection cleared."));
}

FName UBuildModeWidget::GetSelectedBuildPieceID() const
{
	return SelectedBuildPieceID;
}

FName UBuildModeWidget::GetSelectedBuildingPrefabID() const
{
	return SelectedBuildingPrefabID;
}

bool UBuildModeWidget::HasSelectedBuildPiece() const
{
	return bHasSelectedBuildPiece;
}

bool UBuildModeWidget::HasSelectedBuildingPrefab() const
{
	return bHasSelectedBuildingPrefab;
}

bool UBuildModeWidget::GetSelectedBuildPiecePreviewMesh(UStaticMesh*& OutMesh) const
{
	OutMesh = nullptr;

	if (!bHasSelectedBuildPiece || SelectedBuildPieceID.IsNone())
	{
		return false;
	}

	const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem();
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] Build piece preview mesh unavailable: build prefab data subsystem unavailable."));
		return false;
	}

	FWorldForgeBuildPieceRow Row;
	if (!DataSubsystem->GetBuildPieceByID(SelectedBuildPieceID, Row))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] Build piece preview mesh unavailable: BuildID=%s was not found."),
			*SelectedBuildPieceID.ToString());
		return false;
	}

	const FString PreviewMeshPath = Row.PreviewMeshPath.IsEmpty() ? Row.MeshPath : Row.PreviewMeshPath;
	const TCHAR* PreviewMeshFieldName = Row.PreviewMeshPath.IsEmpty()
		? TEXT("MeshPath")
		: TEXT("PreviewMeshPath");

	return WF_LoadPreviewStaticMesh(
		PreviewMeshPath,
		TEXT("Build piece"),
		TEXT("BuildID"),
		SelectedBuildPieceID,
		PreviewMeshFieldName,
		OutMesh);
}

bool UBuildModeWidget::GetSelectedBuildingPrefabPreviewMesh(UStaticMesh*& OutMesh) const
{
	OutMesh = nullptr;

	if (!bHasSelectedBuildingPrefab || SelectedBuildingPrefabID.IsNone())
	{
		return false;
	}

	const UWorldForgeBuildPrefabDataSubsystem* DataSubsystem = GetBuildDataSubsystem();
	if (!DataSubsystem)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] Building prefab preview mesh unavailable: build prefab data subsystem unavailable."));
		return false;
	}

	FWorldForgeBuildingPrefabRow Row;
	if (!DataSubsystem->GetBuildingPrefabByID(SelectedBuildingPrefabID, Row))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BuildModeWidget] Building prefab preview mesh unavailable: PrefabID=%s was not found."),
			*SelectedBuildingPrefabID.ToString());
		return false;
	}

	const FString PreviewMeshPath = Row.PrimaryMeshPath.IsEmpty() ? Row.PreviewMeshPath : Row.PrimaryMeshPath;
	const TCHAR* PreviewMeshFieldName = Row.PrimaryMeshPath.IsEmpty()
		? TEXT("PreviewMeshPath")
		: TEXT("PrimaryMeshPath");

	return WF_LoadPreviewStaticMesh(
		PreviewMeshPath,
		TEXT("Building prefab"),
		TEXT("PrefabID"),
		SelectedBuildingPrefabID,
		PreviewMeshFieldName,
		OutMesh);
}

bool UBuildModeWidget::GetSelectedBuildDataPreviewMesh(UStaticMesh*& OutMesh) const
{
	OutMesh = nullptr;

	if (bHasSelectedBuildPiece)
	{
		return GetSelectedBuildPiecePreviewMesh(OutMesh);
	}

	if (bHasSelectedBuildingPrefab)
	{
		return GetSelectedBuildingPrefabPreviewMesh(OutMesh);
	}

	return false;
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

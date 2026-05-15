#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeColonist.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USpriteCharacterComponent;
class UPaperFlipbookComponent;
class UPaperFlipbook;
class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API AWorldForgeColonist : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeColonist();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 AgentID = -1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 SettlementID = -1;

    void UpdateVisuals(const FAgentData& Data, float DeltaTime, float CellSize = 200.f);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnStateChanged(EAgentState NewState, EAgentState OldState);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnNeedCritical(float FoodLevel, float SleepLevel);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnColonistDied();

    UFUNCTION(BlueprintCallable, Category = "Colonist")
    void InitializeFromAgentData(const FAgentData& Data, float CellSize = 200.f);

    UFUNCTION(BlueprintCallable, Category = "Colonist|Visual")
    void SetSpriteVisualComponent(USpriteCharacterComponent* InSpriteVisual);

    UFUNCTION(BlueprintCallable, Category = "Colonist|Visual")
    void ApplyVisualDefinition(const FWorldForgeColonistVisualRow& VisualDefinition);

    UFUNCTION(BlueprintPure, Category = "Colonist|Visual")
    FWorldForgeColonistVisualRow GetActiveVisualDefinition() const { return ActiveVisualDefinition; }

    UFUNCTION(BlueprintPure, Category = "Colonist|Visual")
    UPaperFlipbookComponent* GetVisualFlipbookComponent() const { return VisualFlipbook; }

    UFUNCTION(BlueprintCallable, Category = "Colonist|Visual")
    bool ApplyVisualForCurrentAgentState();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist")
    float NeedCriticalCooldown = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Visual")
    float TerrainZOffset = 50.0f;

    // Constant visual movement speed in Unreal units per second.
    // Kept high for PIE testing so colonists visibly move between updated sim cells.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Movement")
    float VisualMoveSpeed = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Visual|Debug")
    bool bForceDebugMeshVisible = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Visual|Billboard")
    bool bBillboardFlipbookToCamera = true;

    // PaperFlipbook planes are commonly authored with a sideways facing axis.
    // If the sprite is edge-on, try 0, 90, -90, or 180 in the editor.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Visual|Billboard")
    float FlipbookBillboardYawOffset = 90.0f;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    USceneComponent* SceneRoot = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    UStaticMeshComponent* DebugMesh = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    USpriteCharacterComponent* SpriteVisual = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    UPaperFlipbookComponent* VisualFlipbook = nullptr;

private:
    void UpdateMovement(float DeltaTime);
    FVector ResolveWorldLocationFromAgentData(const FAgentData& Data, float CellSize) const;
    FVector GetCurrentCameraLocation() const;
    void UpdateFlipbookBillboardRotation();

    UPaperFlipbook* LoadFlipbookFromSoftObject(const TSoftObjectPtr<UObject>& VisualAsset) const;
    const TSoftObjectPtr<UObject>& SelectVisualAssetForAgentState() const;
    const TSoftObjectPtr<UObject>& SelectWorkVisualAssetByJob() const;
    bool ApplyVisualAsset(const TSoftObjectPtr<UObject>& VisualAsset);
    void UpdateDebugMeshVisibilityForCurrentVisual(bool bHasValidFlipbook);

    EAgentState LastKnownState = EAgentState::Idle;
    float VisualCellSize = 200.f;
    FVector TargetWorldPos = FVector::ZeroVector;
    float NeedCriticalTimer = 0.f;
    bool bHasVisualData = false;

    FAgentData LastAgentData;
    bool bHasLastAgentData = false;

    FIntPoint CurrentGridCell = FIntPoint::ZeroValue;
    FWorldForgeColonistVisualRow ActiveVisualDefinition;
};
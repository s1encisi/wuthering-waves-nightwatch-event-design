#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/SaveGame.h"
#include "LNData.generated.h"

USTRUCT(BlueprintType)
struct FLNCharacterRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PersonId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRanged = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHealth = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BasicDamage = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BasicInterval = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SkillOneDamage = 18.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SkillTwoDamage = 24.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SkillOne;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SkillTwo;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Source = TEXT("PDF pp.5-6; numeric combat values provisional");
};

USTRUCT(BlueprintType)
struct FLNLampRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RangeCm = 1800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RadiusCm = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WidthCm = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Damage = 85.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Duration = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TickDamage = 12.f;
};

UCLASS()
class LONGNIGHT_API ULNSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 1;
    UPROPERTY() FName ScenarioId = TEXT("M1_Lab");
    UPROPERTY() TArray<FName> Seats;
    UPROPERTY() int32 Module = 0;
    UPROPERTY() float BeamYaw = 0.f;
    UPROPERTY() bool bLabCompleted = false;
};

/** Presentation only: changing these references never changes collision or combat statistics. */
USTRUCT(BlueprintType)
struct FLNVisualRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Move;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Attack;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Skill;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> SkillTwo;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Dodge;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Hit;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Down;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UAnimSequence> Revive;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UStaticMesh> Weapon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<class UTexture2D> Portrait;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector MeshOffset = FVector(0,0,-65);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MeshScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MeshYaw = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName WeaponSocket = TEXT("hand_r");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector WeaponOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator WeaponRotation = FRotator(0,0,90);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WeaponScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Source;
};

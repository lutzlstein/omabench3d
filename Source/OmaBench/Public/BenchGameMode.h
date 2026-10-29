#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BenchGameMode.generated.h"

class UStaticMesh; class UMaterialInterface; class ACameraActor;

UENUM()
enum class EBenchPhase : uint8 { Init, Warmup, Running, Done };

UCLASS()
class OMABENCH_API ABenchGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ABenchGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

private:
	void ParseArgs();
	void ApplyVideoSettings();
	void BuildScene();
	void UpdateCamera(double T);
	void WriteProgress(const TCHAR* State);
	void FinishRun();
	static bool WriteJsonAtomic(const FString& Path, const TSharedRef<class FJsonObject>& Obj);

	EBenchPhase Phase = EBenchPhase::Init;
	double PhaseTime = 0.0, PathTime = 0.0, LastProgress = -10.0, TotalElapsed = 0.0;
	int32 InitFrames = 0;
	int32 Passes = 3, CurrentPass = 0;
	double WarmupS = 10.0, DurationS = 60.0;
	int32 ResX = 2560, ResY = 1440;
	bool bFullscreen = false;
	FString ResultsPath, ProgressPath, RunId, StartedAt;
	TArray<float> CurrentFrames;           // ms, bounded by reserve
	TArray<TArray<float>> PassFrames;
};

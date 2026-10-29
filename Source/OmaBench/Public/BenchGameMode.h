#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BenchGameMode.generated.h"

class UStaticMesh; class UMaterialInterface; class ACameraActor; class UFont;

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
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void AbortRun();

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> TowerMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> SignMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> StreetMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> RainMaterial;
	UPROPERTY() TArray<TObjectPtr<AActor>> Cars;
	UPROPERTY() TObjectPtr<UMaterialInterface> CrtMaterial;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

	// Read-only state for the HUD.
	EBenchPhase GetPhase() const { return Phase; }
	double GetElapsed() const { return TotalElapsed; }
	double GetTotal() const { return WarmupS + DurationS * Passes; }
	int32 GetPass() const { return FMath::Min(CurrentPass + 1, Passes); }
	int32 GetPasses() const { return Passes; }
	double GetLiveFps() const { return LiveFps; }
	const TArray<FString>& GetResultLines() const { return ResultLines; }
	FLinearColor Pal(const TCHAR* Key, FLinearColor Fallback) const { const FLinearColor* C = Palette.Find(Key); return C ? *C : Fallback; }
	FString GetThemeName() const { return ThemeName; }
	int32 GetPixelSize() const { return bNoPostFx ? 0 : PixelSize; }
	bool HudEnabled() const { return !bNoHud; }
	bool IsAborted() const { return bAborted; }
	const class FSysMon* GetSysMon() const { return SysMon.Get(); }
	FString GetRunId() const { return RunId; }
	FString GetMonitorTag() const { return MonitorName.IsEmpty() ? FString(TEXT("AUTO")) : MonitorName + TEXT(" ") + MonitorHz + TEXT("HZ"); }

private:
	void ParseArgs();
	void LoadPalette();
	void ApplyVideoSettings();
	void BuildScene();
	void UpdateCamera(double T);
	void UpdateCars(double T);
	struct FCarLane { FVector Origin; FVector Dir; float Speed; float Len; float Phase; };
	TArray<FCarLane> Lanes;
	void WriteProgress(const TCHAR* State);
	void FinishRun();
	void FillCommon(const TSharedRef<class FJsonObject>& Root) const;
	TSharedPtr<class FSysMon> SysMon; TSharedPtr<class IInputProcessor> EscProc;
	bool bAborted = false, bNoSysmon = false;
	static bool WriteJsonAtomic(const FString& Path, const TSharedRef<class FJsonObject>& Obj);

	EBenchPhase Phase = EBenchPhase::Init;
	double PhaseTime = 0.0, PathTime = 0.0, LastProgress = -10.0, TotalElapsed = 0.0;
	int32 InitFrames = 0;
	int32 Passes = 3, CurrentPass = 0;
	double WarmupS = 10.0, DurationS = 60.0;
	int32 ResX = 2560, ResY = 1440;
	bool bFullscreen = false;
	FString MonitorName, MonitorHz, ResultsPath, ProgressPath, RunId, StartedAt;
	bool bExitRequested = false, bNoHud = false, bNoPostFx = false;
	double LiveFps = 0.0, FpsAccumT = 0.0, DoneHold = 0.0; int32 FpsAccumN = 0; int32 ExitCode = 0; int32 PixelSize = 4;
	TMap<FString, FLinearColor> Palette; FString ThemeName = TEXT("fallback-phosphor");
	TArray<FString> ResultLines;
	TArray<float> CurrentFrames;           // ms, bounded by reserve
	TArray<TArray<float>> PassFrames;
};

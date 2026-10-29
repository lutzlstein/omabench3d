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
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> ChromeMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> FloorMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> EmitMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> HoloMaterial;
	UPROPERTY() TArray<TObjectPtr<AActor>> T1Actors;
	UPROPERTY() TArray<TObjectPtr<AActor>> T2Actors;

	// Read-only state for the HUD.
	EBenchPhase GetPhase() const { return Phase; }
	double GetElapsed() const { return TotalElapsed; }
	double GetTotal() const { return (WarmupS + DurationS * Passes) * FMath::Max(1, TestIds.Num()); }
	FString GetTestId() const { return TestIds.IsValidIndex(CurrentTest) ? TestIds[CurrentTest] : FString(TEXT("T1")); }
	FString GetTestName() const { return TestName(GetTestId()); }
	int32 GetTestIndex() const { return CurrentTest; }
	int32 GetTestCount() const { return TestIds.Num(); }
	static FString TestName(const FString& Id) { return Id == TEXT("T2") ? FString(TEXT("Off-World Hangar")) : FString(TEXT("City Night")); }
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
	void BuildHangar();
	void UpdateHangarCamera(double T);
	void ActivateTest(int32 Index);
	TSharedRef<class FJsonObject> ApplyRtConfig(const FString& Id);
	static const FVector HangarOrigin;
	TArray<FString> TestIds; int32 CurrentTest = 0;
	TArray<TArray<TArray<float>>> TestPassFrames;
	TArray<TSharedPtr<class FJsonObject>> RtConfigs;
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

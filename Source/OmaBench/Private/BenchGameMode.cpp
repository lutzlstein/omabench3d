#include "BenchGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "RHI.h"
#include "DynamicRHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmaBench, Log, All);
static const TCHAR* GAppVersion = TEXT("0.1.0");

ABenchGameMode::ABenchGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; SphereMesh = Sphere.Object; BaseMaterial = Mat.Object;
}

void ABenchGameMode::ParseArgs()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("-passes="), Passes);
	if (FParse::Param(Cmd, TEXT("quick"))) Passes = 1;
	Passes = FMath::Clamp(Passes, 1, 10);
	FParse::Value(Cmd, TEXT("-warmup="), WarmupS);
	FParse::Value(Cmd, TEXT("-duration="), DurationS);
	FString Res;
	if (FParse::Value(Cmd, TEXT("-res="), Res)) { FString A, B; if (Res.Split(TEXT("x"), &A, &B)) { ResX = FCString::Atoi(*A); ResY = FCString::Atoi(*B); } }
	bFullscreen = FParse::Param(Cmd, TEXT("fullscreen"));
	FString Tests; FParse::Value(Cmd, TEXT("-tests="), Tests);
	if (!Tests.IsEmpty() && !Tests.Contains(TEXT("T1"))) UE_LOG(LogOmaBench, Warning, TEXT("Only T1 is implemented in M1; running T1."));
	const FString Dir = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".local/share/omabench/results"));
	if (!FParse::Value(Cmd, TEXT("-runid="), RunId)) RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	if (!FParse::Value(Cmd, TEXT("-results="), ResultsPath)) ResultsPath = FPaths::Combine(Dir, RunId + TEXT(".json"));
	if (!FParse::Value(Cmd, TEXT("-progress="), ProgressPath)) ProgressPath = FPaths::Combine(Dir, TEXT("progress.json"));
	StartedAt = FDateTime::UtcNow().ToIso8601();
}

void ABenchGameMode::ApplyVideoSettings()
{
	auto Set = [](const TCHAR* N, const TCHAR* V){ if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(N)) C->Set(V, ECVF_SetByCode); };
	Set(TEXT("r.VSync"), TEXT("0")); Set(TEXT("t.MaxFPS"), TEXT("0")); Set(TEXT("r.DynamicRes.OperationMode"), TEXT("0"));
	Set(TEXT("r.ScreenPercentage"), TEXT("100")); Set(TEXT("r.MotionBlurQuality"), TEXT("0"));
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		S->SetOverallScalabilityLevel(3);
		S->SetVSyncEnabled(false);
		S->SetFrameRateLimit(0.f);
		S->SetScreenResolution(FIntPoint(ResX, ResY));
		S->SetFullscreenMode(bFullscreen ? EWindowMode::Fullscreen : EWindowMode::Windowed);
		S->ApplySettings(false);
	}
	if (GEngine) GEngine->bUseFixedFrameRate = false, GEngine->bSmoothFrameRate = false;
}

void ABenchGameMode::BuildScene()
{
	UWorld* W = GetWorld();
	FRandomStream Rng(1337);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* City = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USceneComponent* Root = NewObject<USceneComponent>(City, TEXT("Root")); City->SetRootComponent(Root); Root->RegisterComponent();
	auto MakeHISM = [&](UStaticMesh* Mesh, FLinearColor Color, const TCHAR* Name)
	{
		UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(City, Name);
		H->SetStaticMesh(Mesh); H->SetupAttachment(Root);
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, City); M->SetVectorParameterValue(TEXT("Color"), Color);
		H->SetMaterial(0, M); H->SetCollisionEnabled(ECollisionEnabled::NoCollision); H->SetMobility(EComponentMobility::Static);
		H->RegisterComponent(); return H;
	};
	UHierarchicalInstancedStaticMeshComponent* Ground = MakeHISM(CubeMesh, FLinearColor(0.2f,0.2f,0.22f), TEXT("Ground"));
	UHierarchicalInstancedStaticMeshComponent* Towers = MakeHISM(CubeMesh, FLinearColor(0.7f,0.7f,0.75f), TEXT("Towers"));
	UHierarchicalInstancedStaticMeshComponent* Orbs = MakeHISM(SphereMesh, FLinearColor(0.9f,0.5f,0.2f), TEXT("Orbs"));
	Ground->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0,0,-50), FVector(800,800,1)));
	const int32 N = 80; const float Spacing = 900.f; int32 Count = 0;
	for (int32 X = 0; X < N; ++X) for (int32 Y = 0; Y < N; ++Y)
	{
		const FVector Base((X - N/2) * Spacing, (Y - N/2) * Spacing, 0);
		if (Rng.FRand() < 0.15f) continue; // streets
		const int32 Stack = Rng.RandRange(2, 6); float Z = 0;
		for (int32 S = 0; S < Stack; ++S)
		{
			const float H = Rng.FRandRange(200, 1400), Wd = Rng.FRandRange(3, 7.5f) * (1.f - S*0.12f);
			Towers->AddInstance(FTransform(FRotator(0, Rng.FRandRange(0, 90), 0), Base + FVector(0,0,Z + H*0.5f), FVector(Wd, Wd, H/100.f)));
			Z += H; ++Count;
		}
		for (int32 O = 0; O < 3; ++O)
		{ Orbs->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(Rng.FRandRange(-400,400), Rng.FRandRange(-400,400), Rng.FRandRange(50, Z+300)), FVector(Rng.FRandRange(0.5f,2.5f)))); ++Count; }
	}
	// Lights
	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-35, 40, 0)), P);
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetLightComponent()->SetIntensity(8.f); Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
	AActor* Sky = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(Sky, TEXT("Atmo")); Sky->SetRootComponent(Atmo); Atmo->RegisterComponent();
	ASkyLight* SkyL = W->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, P);
	SkyL->GetLightComponent()->SetMobility(EComponentMobility::Movable); SkyL->GetLightComponent()->bRealTimeCapture = true; SkyL->GetLightComponent()->RecaptureSky();
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, P);
	Fog->GetComponent()->SetVolumetricFog(true); Fog->GetComponent()->SetFogDensity(0.01f);
	const int32 NumLights = 96;
	for (int32 L = 0; L < NumLights; ++L)
	{
		const FVector Pos(Rng.FRandRange(-N*Spacing*0.3f, N*Spacing*0.3f), Rng.FRandRange(-N*Spacing*0.3f, N*Spacing*0.3f), Rng.FRandRange(300, 3000));
		APointLight* PL = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(Pos), P);
		UPointLightComponent* C = Cast<UPointLightComponent>(PL->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable); C->SetIntensity(Rng.FRandRange(20000, 80000)); C->SetAttenuationRadius(4000);
		C->SetLightColor(FLinearColor::MakeFromHSV8(uint8(Rng.RandRange(0,255)), 200, 255)); C->SetCastShadows(true);
	}
	UE_LOG(LogOmaBench, Log, TEXT("T1 scene: %d mesh instances, %d point lights"), Count, NumLights);
	Camera = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	Camera->GetCameraComponent()->SetFieldOfView(80.f); Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	UpdateCamera(0);
}

void ABenchGameMode::UpdateCamera(double T)
{
	if (!Camera) return;
	auto PosAt = [](double t){ const double w = 2*PI/60.0; return FVector(22000*FMath::Cos(w*t), 15000*FMath::Sin(2*w*t), 2500 + 1500*FMath::Sin(3*w*t)); };
	const FVector Pos = PosAt(T), Ahead = PosAt(T + 1.5) - FVector(0,0,600);
	Camera->SetActorLocationAndRotation(Pos, (Ahead - Pos).Rotation());
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
}

void ABenchGameMode::BeginPlay()
{
	Super::BeginPlay();
	ParseArgs();
	ApplyVideoSettings();
	BuildScene();
	UE_LOG(LogOmaBench, Log, TEXT("OmaBench %s run %s: passes=%d warmup=%.0fs duration=%.0fs res=%dx%d fullscreen=%d results=%s"),
		GAppVersion, *RunId, Passes, WarmupS, DurationS, ResX, ResY, bFullscreen, *ResultsPath);
}

void ABenchGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FApp::GetDeltaTime(); // real wall-clock frame time
	if (Phase == EBenchPhase::Done) return;
	if (Phase == EBenchPhase::Init) { if (++InitFrames > 5) { Phase = EBenchPhase::Warmup; PhaseTime = 0; } return; }
	TotalElapsed += Dt; PhaseTime += Dt; PathTime += Dt;
	UpdateCamera(FMath::Fmod(PathTime, 60.0));
	if (Phase == EBenchPhase::Warmup)
	{
		if (PhaseTime >= WarmupS) { Phase = EBenchPhase::Running; PhaseTime = 0; PathTime = 0; CurrentFrames.Reset(); CurrentFrames.Reserve(100000); }
	}
	else if (Phase == EBenchPhase::Running)
	{
		if (CurrentFrames.Num() < 2000000) CurrentFrames.Add(float(Dt * 1000.0));
		if (PhaseTime >= DurationS)
		{
			PassFrames.Add(MoveTemp(CurrentFrames)); CurrentFrames = {};
			if (++CurrentPass >= Passes) { FinishRun(); return; }
			PhaseTime = 0; PathTime = 0; CurrentFrames.Reserve(100000);
		}
	}
	if (TotalElapsed - LastProgress >= 1.0) { LastProgress = TotalElapsed; WriteProgress(Phase == EBenchPhase::Warmup ? TEXT("warmup") : TEXT("running")); }
}

bool ABenchGameMode::WriteJsonAtomic(const FString& Path, const TSharedRef<FJsonObject>& Obj)
{
	FString Out; auto Wr = TJsonWriterFactory<>::Create(&Out); FJsonSerializer::Serialize(Obj, Wr);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const FString Tmp = Path + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Out, *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path, *Tmp, true, true);
}

void ABenchGameMode::WriteProgress(const TCHAR* State)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("schema"), 1); O->SetStringField(TEXT("runId"), RunId); O->SetStringField(TEXT("state"), State);
	O->SetStringField(TEXT("test"), TEXT("T1")); O->SetNumberField(TEXT("pass"), FMath::Min(CurrentPass + 1, Passes)); O->SetNumberField(TEXT("passes"), Passes);
	O->SetNumberField(TEXT("elapsedS"), FMath::RoundToDouble(TotalElapsed * 10) / 10); O->SetNumberField(TEXT("totalS"), WarmupS + DurationS * Passes);
	WriteJsonAtomic(ProgressPath, O);
}

struct FPassStats { double AvgFps=0, P1=0, P01=0, Std=0, Cov=0, Score=0, AvgMs=0; int32 Frames=0; };
static FPassStats Compute(const TArray<float>& F)
{
	FPassStats S; S.Frames = F.Num(); if (F.Num() < 2) return S;
	double Sum = 0; for (float v : F) Sum += v; S.AvgMs = Sum / F.Num();
	S.AvgFps = 1000.0 * F.Num() / Sum; // harmonic mean of per-frame FPS
	double Var = 0; for (float v : F) Var += FMath::Square(v - S.AvgMs); S.Std = FMath::Sqrt(Var / (F.Num() - 1)); S.Cov = S.Std / S.AvgMs;
	TArray<float> Sorted = F; Sorted.Sort();
	auto Pct = [&](double p){ return Sorted[FMath::Clamp(int32(FMath::CeilToDouble(p * Sorted.Num())) - 1, 0, Sorted.Num()-1)]; };
	S.P1 = 1000.0 / Pct(0.99); S.P01 = 1000.0 / Pct(0.999);
	const double K = 100.0; // provisional; calibrate against reference GPU after M3
	S.Score = K * S.AvgFps * FMath::Clamp(1.0 - S.Cov, 0.8, 1.0);
	return S;
}
static double R2(double v){ return FMath::RoundToDouble(v * 100) / 100; }

void ABenchGameMode::FinishRun()
{
	Phase = EBenchPhase::Done;
	TArray<FPassStats> Stats; for (auto& F : PassFrames) Stats.Add(Compute(F));
	TArray<int32> Idx; for (int32 i = 0; i < Stats.Num(); ++i) Idx.Add(i);
	Idx.Sort([&](int32 a, int32 b){ return Stats[a].Score < Stats[b].Score; });
	const FPassStats& Med = Stats[Idx[Idx.Num()/2]];
	double MinS = Stats[Idx[0]].Score, MaxS = Stats[Idx.Last()].Score;
	const bool bStable = Med.Score > 0 && (MaxS - MinS) / Med.Score <= 0.03;

	auto StatObj = [](const FPassStats& S){ TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("score"), FMath::RoundToDouble(S.Score)); O->SetNumberField(TEXT("avgFps"), R2(S.AvgFps));
		O->SetNumberField(TEXT("p1LowFps"), R2(S.P1)); O->SetNumberField(TEXT("p01LowFps"), R2(S.P01));
		O->SetNumberField(TEXT("frameTimeMsAvg"), R2(S.AvgMs)); O->SetNumberField(TEXT("frameTimeMsStd"), R2(S.Std));
		O->SetNumberField(TEXT("cov"), FMath::RoundToDouble(S.Cov * 10000) / 10000); O->SetNumberField(TEXT("frames"), S.Frames); return O; };

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1); Root->SetStringField(TEXT("runId"), RunId);
	Root->SetStringField(TEXT("appVersion"), GAppVersion); Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
	Root->SetStringField(TEXT("startedAt"), StartedAt);
	TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
	Env->SetStringField(TEXT("gpu"), GRHIAdapterName); Env->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
	Env->SetStringField(TEXT("kernel"), FPlatformMisc::GetOSVersion()); Env->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
	Env->SetNumberField(TEXT("ramGb"), (double)FPlatformMemory::GetConstants().TotalPhysicalGB);
	Env->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : TEXT("unknown"));
	Root->SetObjectField(TEXT("env"), Env);
	TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
	Set->SetStringField(TEXT("res"), FString::Printf(TEXT("%dx%d"), ResX, ResY)); Set->SetNumberField(TEXT("passes"), Passes);
	Set->SetStringField(TEXT("preset"), Passes == 1 ? TEXT("quick") : TEXT("standard")); Set->SetBoolField(TEXT("fullscreen"), bFullscreen);
	Set->SetNumberField(TEXT("warmupS"), WarmupS); Set->SetNumberField(TEXT("durationS"), DurationS);
	Root->SetObjectField(TEXT("settings"), Set);
	TSharedRef<FJsonObject> T1 = StatObj(Med); T1->SetStringField(TEXT("id"), TEXT("T1")); T1->SetBoolField(TEXT("skipped"), false);
	TArray<TSharedPtr<FJsonValue>> PassArr; for (auto& S : Stats) PassArr.Add(MakeShared<FJsonValueObject>(StatObj(S)));
	T1->SetArrayField(TEXT("passes"), PassArr);
	Root->SetArrayField(TEXT("tests"), { MakeShared<FJsonValueObject>(T1) });
	TSharedRef<FJsonObject> Scores = MakeShared<FJsonObject>(); Scores->SetNumberField(TEXT("gpu"), FMath::RoundToDouble(Med.Score)); Scores->SetField(TEXT("cpu"), MakeShared<FJsonValueNull>());
	Root->SetObjectField(TEXT("scores"), Scores); Root->SetBoolField(TEXT("stable"), bStable);
	const bool bOk = WriteJsonAtomic(ResultsPath, Root);
	WriteProgress(bOk ? TEXT("done") : TEXT("failed"));
	UE_LOG(LogOmaBench, Display, TEXT("OmaBench done: score=%.0f avgFps=%.1f p1=%.1f results=%s ok=%d"), Med.Score, Med.AvgFps, Med.P1, *ResultsPath, bOk);
	FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
}

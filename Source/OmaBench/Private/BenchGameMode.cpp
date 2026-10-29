#include "BenchGameMode.h"
#include "BenchHUD.h"
#include "Engine/PostProcessVolume.h"
#include "Components/SkyAtmosphereComponent.h"
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tower(TEXT("/Game/OmaBench/Materials/M_Tower.M_Tower"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sign(TEXT("/Game/OmaBench/Materials/M_Sign.M_Sign"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Street(TEXT("/Game/OmaBench/Materials/M_Street.M_Street"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Crt(TEXT("/Game/OmaBench/Materials/PP_CRT.PP_CRT"));
	CubeMesh = Cube.Object; SphereMesh = Sphere.Object; BaseMaterial = Mat.Object;
	TowerMaterial = Tower.Object; SignMaterial = Sign.Object; StreetMaterial = Street.Object; CrtMaterial = Crt.Object;
	HUDClass = ABenchHUD::StaticClass();
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
	bNoHud = FParse::Param(Cmd, TEXT("nohud"));
	bNoPostFx = FParse::Param(Cmd, TEXT("nopostfx"));
	PixelSize = FMath::Max(1, FMath::RoundToInt(ResY / 360.f));
	FParse::Value(Cmd, TEXT("-pixel="), PixelSize); PixelSize = FMath::Clamp(PixelSize, 0, 16);
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

void ABenchGameMode::LoadPalette()
{
	// Omarchy theme palette (sRGB hex in colors.toml). Fallback: green/amber phosphor.
	Palette.Add(TEXT("background"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#020a04"))));
	Palette.Add(TEXT("foreground"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ff66"))));
	Palette.Add(TEXT("accent"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ff66"))));
	Palette.Add(TEXT("bright_yellow"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ffb000"))));
	Palette.Add(TEXT("bright_cyan"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ffcc"))));
	Palette.Add(TEXT("bright_magenta"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ff9933"))));
	Palette.Add(TEXT("muted"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2f6b3f"))));
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	const FString Cands[] = { Home / TEXT(".config/omarchy/current/theme"), Home / TEXT(".local/state/omarchy/current/theme") };
	for (const FString& Dir : Cands)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(Dir / TEXT("colors.toml")))) continue;
		for (const FString& L : Lines)
		{
			FString K, V; if (!L.Split(TEXT("="), &K, &V)) continue;
			K.TrimStartAndEndInline(); V.TrimStartAndEndInline(); V.ReplaceInline(TEXT("\""), TEXT(""));
			if (V.StartsWith(TEXT("#")) && V.Len() == 7) Palette.Add(K, FLinearColor::FromSRGBColor(FColor::FromHex(V)));
		}
		FString Name; if (FFileHelper::LoadFileToString(Name, *(FPaths::GetPath(Dir) / TEXT("theme.name")))) ThemeName = Name.TrimStartAndEnd().Left(48);
		else ThemeName = TEXT("omarchy");
		break;
	}
	UE_LOG(LogOmaBench, Log, TEXT("Palette: %s (%d colors)"), *ThemeName, Palette.Num());
}

void ABenchGameMode::BuildScene()
{
	// Night megacity. Everything comes from one seeded RNG, so the scene is identical every run.
	UWorld* W = GetWorld();
	FRandomStream Rng(2077);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FLinearColor Neon[] = { Pal(TEXT("bright_magenta"), FLinearColor(1,0.1f,0.6f)), Pal(TEXT("bright_cyan"), FLinearColor(0,0.9f,1)),
		Pal(TEXT("bright_yellow"), FLinearColor(1,0.8f,0)), Pal(TEXT("bright_red"), FLinearColor(1,0.2f,0.2f)), Pal(TEXT("bright_blue"), FLinearColor(0.3f,0.4f,1)) };
	const FLinearColor Warm = Pal(TEXT("light_foreground"), FLinearColor(1,0.8f,0.6f));
	const FLinearColor Accent = Pal(TEXT("accent"), FLinearColor(0.5f,0.5f,0.9f));

	AActor* City = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USceneComponent* Root = NewObject<USceneComponent>(City, TEXT("Root")); Root->SetMobility(EComponentMobility::Static); City->SetRootComponent(Root); Root->RegisterComponent();
	auto MakeHISM = [&](UStaticMesh* Mesh, UMaterialInterface* Base, const FString& Name, TFunctionRef<void(UMaterialInstanceDynamic*)> Setup)
	{
		UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(City, *Name);
		H->SetStaticMesh(Mesh); H->SetupAttachment(Root);
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base ? Base : BaseMaterial.Get(), City); Setup(M);
		H->SetMaterial(0, M); H->SetCollisionEnabled(ECollisionEnabled::NoCollision); H->SetMobility(EComponentMobility::Static);
		H->RegisterComponent(); return H;
	};
	UHierarchicalInstancedStaticMeshComponent* Ground = MakeHISM(CubeMesh, StreetMaterial, TEXT("Street"), [](UMaterialInstanceDynamic*){});
	UHierarchicalInstancedStaticMeshComponent* Towers[2] = {
		MakeHISM(CubeMesh, TowerMaterial, TEXT("TowersWarm"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Warm); }),
		MakeHISM(CubeMesh, TowerMaterial, TEXT("TowersCool"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Neon[1]); M->SetScalarParameterValue(TEXT("LitFraction"), 0.3f); }) };
	UHierarchicalInstancedStaticMeshComponent* Signs[5];
	for (int32 i = 0; i < 5; ++i) Signs[i] = MakeHISM(CubeMesh, SignMaterial, FString::Printf(TEXT("Signs%d"), i), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), Neon[i]); });
	UHierarchicalInstancedStaticMeshComponent* Clutter = MakeHISM(CubeMesh, TowerMaterial, TEXT("Clutter"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Neon[2]); M->SetScalarParameterValue(TEXT("LitFraction"), 0.15f); });
	Ground->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0,0,-50), FVector(1400,1400,1)));

	const int32 N = 110; const float Cell = 1000.f; int32 Count = 0, SignCount = 0;
	TArray<FVector> SignLights;
	for (int32 X = 0; X < N; ++X) for (int32 Y = 0; Y < N; ++Y)
	{
		const int32 GX = X - N/2, GY = Y - N/2;
		const FVector Base(GX * Cell, GY * Cell, 0);
		const bool bAvenue = FMath::Abs(GX) <= 1;           // the canyon the camera flies through
		const bool bStreet = bAvenue || (GX % 5 == 0) || (GY % 6 == 0);
		if (bStreet)
		{
			// street clutter: kiosks, parked vans, barriers
			if (!bAvenue || Rng.FRand() < 0.5f)
				for (int32 k = 0; k < 2; ++k)
				{ Clutter->AddInstance(FTransform(FRotator(0, Rng.FRandRange(0,180), 0), Base + FVector(Rng.FRandRange(-400,400), Rng.FRandRange(-400,400), 60), FVector(Rng.FRandRange(0.6f,3.f), Rng.FRandRange(0.6f,1.5f), Rng.FRandRange(0.8f,2.f)))); ++Count; }
			continue;
		}
		// Megablock tower: taller near the canyon, stacked setbacks.
		const float Near = FMath::Clamp(1.f - FMath::Abs(GX) / 30.f, 0.15f, 1.f);
		const int32 Stack = Rng.RandRange(3, 8); float Z = 0;
		UHierarchicalInstancedStaticMeshComponent* T = Towers[Rng.RandRange(0, 1)];
		float Wd = Rng.FRandRange(6.5f, 9.2f);
		for (int32 S = 0; S < Stack; ++S)
		{
			const float H = Rng.FRandRange(800, 3200) * (0.4f + Near);
			T->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0,0,Z + H*0.5f), FVector(Wd, Wd * Rng.FRandRange(0.8f,1.f), H/100.f)));
			// neon billboards on faces, biased toward the canyon side
			const int32 NS = Rng.RandRange(0, 3);
			for (int32 k = 0; k < NS; ++k)
			{
				const float Side = (GX > 0) ? -1.f : 1.f;
				const bool bFacing = Rng.FRand() < 0.7f;
				const float Off = Wd * 50.f + 40.f;
				const FVector Pos = Base + (bFacing ? FVector(Side * Off, Rng.FRandRange(-250,250), 0) : FVector(Rng.FRandRange(-250,250), (Rng.FRand()<0.5f?-1:1) * Off, 0)) + FVector(0,0,Z + Rng.FRandRange(0.15f,0.85f)*H);
				const FVector Scale = bFacing ? FVector(0.1f, Rng.FRandRange(1.f,4.f), Rng.FRandRange(1.5f,6.f)) : FVector(Rng.FRandRange(1.f,4.f), 0.1f, Rng.FRandRange(0.6f,2.f));
				Signs[Rng.RandRange(0, 4)]->AddInstance(FTransform(FRotator::ZeroRotator, Pos, Scale)); ++SignCount;
				if (bFacing && FMath::Abs(GX) <= 4 && Pos.Z < 9000) SignLights.Add(Pos + FVector(Side * 150, 0, 0));
			}
			Z += H; Wd *= Rng.FRandRange(0.72f, 0.95f); ++Count;
		}
	}
	// Moonless sky, dim accent-tinted fill, dense wet haze.
	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-20, 70, 0)), P);
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetLightComponent()->SetIntensity(0.25f); Sun->GetLightComponent()->SetLightColor(Accent);
	Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
	AActor* Sky = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(Sky, TEXT("Atmo")); Sky->SetRootComponent(Atmo); Atmo->RegisterComponent();
	ASkyLight* SkyL = W->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, P);
	SkyL->GetLightComponent()->SetMobility(EComponentMobility::Movable); SkyL->GetLightComponent()->bRealTimeCapture = true;
	SkyL->GetLightComponent()->SetIntensity(0.4f); SkyL->GetLightComponent()->RecaptureSky();
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, P);
	UExponentialHeightFogComponent* FogC = Fog->GetComponent();
	FogC->SetVolumetricFog(true); FogC->SetFogDensity(0.035f); FogC->SetFogHeightFalloff(0.05f);
	FogC->SetFogInscatteringColor(Accent * 0.05f); FogC->SetVolumetricFogAlbedo(FColor(220, 210, 230)); FogC->SetVolumetricFogExtinctionScale(1.5f);
	FogC->SetVolumetricFogScatteringDistribution(0.7f);
	FogC->SecondFogData.FogDensity = 0.25f; FogC->SecondFogData.FogHeightFalloff = 0.6f; FogC->SecondFogData.FogHeightOffset = 0.f; // ground-level steam layer
	FogC->MarkRenderStateDirty();
	// Sign spill lights along the canyon (shadowed) + scattered street lights.
	int32 NumLights = 0;
	for (int32 i = 0; i < SignLights.Num() && NumLights < 140; i += 2)
	{
		APointLight* PL = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(SignLights[i]), P);
		UPointLightComponent* C = Cast<UPointLightComponent>(PL->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable); C->SetIntensity(Rng.FRandRange(40000, 120000)); C->SetAttenuationRadius(3500);
		C->SetLightColor(Neon[Rng.RandRange(0, 4)]); C->SetCastShadows(true); C->SetVolumetricScatteringIntensity(3.f); ++NumLights;
	}
	for (int32 L = 0; L < 60; ++L)
	{
		const FVector Pos(Rng.FRandRange(-1500, 1500), Rng.FRandRange(-50000, 50000), Rng.FRandRange(150, 500));
		APointLight* PL = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(Pos), P);
		UPointLightComponent* C = Cast<UPointLightComponent>(PL->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable); C->SetIntensity(30000); C->SetAttenuationRadius(2200);
		C->SetLightColor(Warm); C->SetCastShadows(true); ++NumLights;
	}
	// Flying traffic: emissive pods with unshadowed lights on deterministic lanes.
	UMaterialInstanceDynamic* CarMat[2];
	for (int32 i = 0; i < 2; ++i) { CarMat[i] = UMaterialInstanceDynamic::Create(SignMaterial, this); CarMat[i]->SetVectorParameterValue(TEXT("Color"), i ? FLinearColor(1,0.05f,0.05f) : FLinearColor(1,0.95f,0.85f)); CarMat[i]->SetScalarParameterValue(TEXT("Glow"), 40.f); }
	for (int32 i = 0; i < 64; ++i)
	{
		FCarLane Lane; const bool bAlong = i < 48;
		Lane.Origin = bAlong ? FVector(Rng.FRandRange(-1100, 1100), (i % 2) ? -55000 : 55000, Rng.FRandRange(1200, 6000)) : FVector(-55000, (Rng.RandRange(-9, 9)) * 6 * Cell, Rng.FRandRange(2500, 8000));
		Lane.Dir = bAlong ? FVector(0, (i % 2) ? 1 : -1, 0) : FVector(1, 0, 0);
		Lane.Speed = Rng.FRandRange(1500, 4000); Lane.Len = 110000; Lane.Phase = Rng.FRandRange(0, Lane.Len);
		Lanes.Add(Lane);
		AActor* Car = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Car, TEXT("Body"));
		Body->SetMobility(EComponentMobility::Movable); Body->SetStaticMesh(CubeMesh); Body->SetWorldScale3D(bAlong ? FVector(0.7f, 1.8f, 0.4f) : FVector(1.8f, 0.7f, 0.4f));
		Body->SetMaterial(0, CarMat[(i % 2) ? 1 : 0]); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Body->SetCastShadow(false);
		Car->SetRootComponent(Body); Body->RegisterComponent();
		UPointLightComponent* CL = NewObject<UPointLightComponent>(Car, TEXT("Light"));
		CL->SetupAttachment(Body); CL->SetMobility(EComponentMobility::Movable); CL->SetIntensity(20000); CL->SetAttenuationRadius(1800);
		CL->SetLightColor((i % 2) ? FLinearColor(1,0.1f,0.1f) : FLinearColor(1,0.95f,0.85f)); CL->SetCastShadows(false); CL->RegisterComponent();
		Cars.Add(Car); NumLights++;
	}
	UpdateCars(0);
	UE_LOG(LogOmaBench, Log, TEXT("T1 night city: %d building/clutter instances, %d neon signs, %d lights, %d flying cars"), Count, SignCount, NumLights, Cars.Num());
	// Retro post: bloom, slight chromatic fringe, vignette, scanline material. Fixed values for repeatability.
	APostProcessVolume* PPV = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, P);
	PPV->bUnbound = true; FPostProcessSettings& S = PPV->Settings;
	S.bOverride_BloomIntensity = true; S.BloomIntensity = 1.2f; S.bOverride_BloomThreshold = true; S.BloomThreshold = 0.8f;
	S.bOverride_VignetteIntensity = true; S.VignetteIntensity = bNoPostFx ? 0.f : 0.7f;
	S.bOverride_SceneFringeIntensity = true; S.SceneFringeIntensity = bNoPostFx ? 0.f : 1.2f;
	S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 1.f;
	S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.f; // fixed exposure -> consistent frames
	S.bOverride_MotionBlurAmount = true; S.MotionBlurAmount = 0.f;
	if (CrtMaterial && !bNoPostFx)
	{
		UMaterialInstanceDynamic* Crt = UMaterialInstanceDynamic::Create(CrtMaterial, this);
		// Pixel size scales with output height so the virtual grid is ~360 rows at the default (4 px @ 1440p, 3 @ 1080p).
		Crt->SetScalarParameterValue(TEXT("PixelSize"), float(PixelSize));
		Crt->SetScalarParameterValue(TEXT("Levels"), PixelSize > 0 ? 10.f : 0.f);
		Crt->SetScalarParameterValue(TEXT("Scanline"), PixelSize > 1 ? 0.3f : 0.2f);
		S.AddBlendable(Crt, 1.f);
	}
	UE_LOG(LogOmaBench, Log, TEXT("PostFX=%d pixel=%d"), !bNoPostFx, PixelSize);
	Camera = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	Camera->GetCameraComponent()->SetFieldOfView(80.f); Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	UpdateCamera(0);
}

void ABenchGameMode::UpdateCars(double T)
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		const FCarLane& L = Lanes[i];
		const double D = FMath::Fmod(L.Phase + L.Speed * T, (double)L.Len);
		Cars[i]->SetActorLocation(L.Origin + L.Dir * D);
	}
}

void ABenchGameMode::UpdateCamera(double T)
{
	if (!Camera) return;
	// 60 s canyon run down the central avenue: weaving, climbing and diving between megablocks.
	auto PosAt = [](double t){ const double u = t / 60.0; return FVector(650*FMath::Sin(2*PI*u*3), -42000 + 84000*u, 2200 + 1400*FMath::Sin(2*PI*u*2) + 600*FMath::Sin(2*PI*u*5)); };
	const FVector Pos = PosAt(T), Ahead = PosAt(T + 1.2) - FVector(0,0,250);
	FRotator R = (Ahead - Pos).Rotation(); R.Roll = -8.0 * FMath::Cos(2*PI*(T/60.0)*3);
	Camera->SetActorLocationAndRotation(Pos, R);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
}

void ABenchGameMode::BeginPlay()
{
	Super::BeginPlay();
	ParseArgs();
	LoadPalette();
	ApplyVideoSettings();
	BuildScene();
	UE_LOG(LogOmaBench, Log, TEXT("OmaBench %s run %s: passes=%d warmup=%.0fs duration=%.0fs res=%dx%d fullscreen=%d results=%s"),
		GAppVersion, *RunId, Passes, WarmupS, DurationS, ResX, ResY, bFullscreen, *ResultsPath);
}

void ABenchGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FApp::GetDeltaTime(); // real wall-clock frame time
	FpsAccumT += FApp::GetDeltaTime(); ++FpsAccumN;
	if (FpsAccumT >= 0.5) { LiveFps = FpsAccumN / FpsAccumT; FpsAccumT = 0; FpsAccumN = 0; }
	if (Phase == EBenchPhase::Done)
	{
		DoneHold += FApp::GetDeltaTime();
		if (!bExitRequested && DoneHold >= (bNoHud ? 0.0 : 4.0)) { bExitRequested = true; FPlatformMisc::RequestExitWithStatus(false, ExitCode); }
		return;
	}
	if (Phase == EBenchPhase::Init) { if (++InitFrames > 5) { Phase = EBenchPhase::Warmup; PhaseTime = 0; } return; }
	TotalElapsed += Dt; PhaseTime += Dt; PathTime += Dt;
	UpdateCamera(FMath::Fmod(PathTime, 60.0));
	UpdateCars(PathTime);
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
	Set->SetNumberField(TEXT("warmupS"), WarmupS); Set->SetStringField(TEXT("theme"), ThemeName); Set->SetBoolField(TEXT("postFx"), !bNoPostFx); Set->SetNumberField(TEXT("pixel"), bNoPostFx ? 0 : PixelSize); Set->SetBoolField(TEXT("hud"), !bNoHud); Set->SetNumberField(TEXT("durationS"), DurationS);
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
	ExitCode = bOk ? 0 : 1;
	ResultLines = {
		FString::Printf(TEXT("GPU SCORE ......... %.0f"), Med.Score),
		FString::Printf(TEXT("AVG FPS ........... %.1f"), Med.AvgFps),
		FString::Printf(TEXT("1%% LOW ............ %.1f"), Med.P1),
		FString::Printf(TEXT("0.1%% LOW .......... %.1f"), Med.P01),
		FString::Printf(TEXT("FRAME STDDEV ...... %.2f MS"), Med.Std),
		FString::Printf(TEXT("STABLE ............ %s"), bStable ? TEXT("YES") : TEXT("NO")),
		FString::Printf(TEXT("RESULTS -> %s"), *FPaths::GetCleanFilename(ResultsPath)),
	};
}

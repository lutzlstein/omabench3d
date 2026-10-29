// T2 "Off-World Hangar": procedural spinner hangar bay built from engine basic shapes.
// Fixed seed, no assets beyond /Engine/BasicShapes and the generated OmaBench materials.
#include "BenchGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/LocalFogVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"

const FVector ABenchGameMode::HangarOrigin(0, 0, 300000);

void ABenchGameMode::BuildHangar()
{
	UWorld* W = GetWorld();
	FRandomStream Rng(2049);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector O = HangarOrigin;
	const FLinearColor Cyan = Pal(TEXT("bright_cyan"), FLinearColor(0, 0.9f, 1)), Mag = Pal(TEXT("bright_magenta"), FLinearColor(1, 0.1f, 0.6f));
	const FLinearColor Amber = Pal(TEXT("bright_yellow"), FLinearColor(1, 0.7f, 0.1f)), Red = Pal(TEXT("bright_red"), FLinearColor(1, 0.15f, 0.1f));
	const FLinearColor Blue = Pal(TEXT("bright_blue"), FLinearColor(0.3f, 0.45f, 1)), Accent = Pal(TEXT("accent"), FLinearColor(0.5f, 0.5f, 0.9f));
	const FLinearColor Warm = Pal(TEXT("light_foreground"), FLinearColor(1, 0.85f, 0.7f));

	AActor* Bay = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform(O), P);
	USceneComponent* Root = NewObject<USceneComponent>(Bay, TEXT("HangarRoot")); Root->SetMobility(EComponentMobility::Static);
	Root->SetRelativeLocation(O); Bay->SetRootComponent(Root); Root->RegisterComponent(); // static: place before register
	auto Mat = [&](UMaterialInterface* Base, TFunctionRef<void(UMaterialInstanceDynamic*)> F) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base ? Base : BaseMaterial.Get(), Bay); F(M); return M; };
	UMaterialInstanceDynamic* Chrome = Mat(ChromeMaterial, [](UMaterialInstanceDynamic*){});
	UMaterialInstanceDynamic* Gun = Mat(ChromeMaterial, [](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Base"), FLinearColor(0.18f, 0.19f, 0.22f)); M->SetScalarParameterValue(TEXT("Roughness"), 0.12f); });
	UMaterialInstanceDynamic* Panel = Mat(ChromeMaterial, [](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Base"), FLinearColor(0.09f, 0.09f, 0.1f)); M->SetScalarParameterValue(TEXT("Roughness"), 0.22f); });
	UMaterialInstanceDynamic* Floor = Mat(FloorMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("LineColor"), Cyan * 0.6f); });
	UMaterialInstanceDynamic* Glass = Mat(GlassMaterial, [](UMaterialInstanceDynamic*){});
	UMaterialInstanceDynamic* Canopy = Mat(GlassMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.2f, 0.3f, 0.4f)); M->SetScalarParameterValue(TEXT("Opacity"), 0.45f); });
	auto Emit = [&](const FLinearColor& C, float I) { return Mat(EmitMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), C * I); }); };
	auto Holo = [&](const FLinearColor& C) { return Mat(HoloMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), C * 3.f); }); };

	int32 NComp = 0;
	auto Part = [&](UStaticMesh* Mesh, UMaterialInterface* M, const FVector& Loc, const FRotator& Rot, const FVector& Scale, bool bShadow = true) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Bay, *FString::Printf(TEXT("H%d"), NComp++));
		C->SetStaticMesh(Mesh); C->SetMaterial(0, M); C->SetMobility(EComponentMobility::Static); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Root); C->SetCastShadow(bShadow);
		C->SetRelativeTransform(FTransform(Rot, Loc, Scale)); C->RegisterComponent(); return C; }; // static: place before register
	auto Box = [&](UMaterialInterface* M, const FVector& Loc, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true) { return Part(CubeMesh, M, Loc, Rot, Size / 100.f, bShadow); };

	// Shell: 90 m x 60 m x 20 m bay. Mirror floor, panelled walls/ceiling, window wall at +X.
	const float HX = 4500, HY = 3000, HZ = 2000;
	Box(Floor, FVector(0, 0, -10), FVector(2 * HX, 2 * HY, 20));
	Box(Panel, FVector(0, 0, HZ + 20), FVector(2 * HX, 2 * HY, 40));
	Box(Panel, FVector(-HX - 20, 0, HZ / 2), FVector(40, 2 * HY, HZ));
	for (int32 s = -1; s <= 1; s += 2) Box(Panel, FVector(0, s * (HY + 20), HZ / 2), FVector(2 * HX, 40, HZ));
	// Ribbed wall columns (chrome) for reflections to chew on.
	for (int32 i = -8; i <= 8; ++i) for (int32 s = -1; s <= 1; s += 2)
		Box(i % 2 ? Gun : Chrome, FVector(i * 520.f, s * (HY - 40), HZ / 2), FVector(80, 60, HZ));
	// Ceiling beams + overhead light strips (emissive mesh + rect light, RT soft shadows).
	for (int32 i = -4; i <= 4; ++i)
	{
		Box(Gun, FVector(i * 1000.f, 0, HZ - 60), FVector(120, 2 * HY, 120));
		Box(Emit(Warm, 14.f), FVector(i * 1000.f + 300, 0, HZ - 8), FVector(60, 2 * HY - 600, 12), FRotator::ZeroRotator, false);
		if (i % 2 == 0) continue;
		for (int32 s = -1; s <= 1; s += 2)
		{
			URectLightComponent* L = NewObject<URectLightComponent>(Bay, *FString::Printf(TEXT("Strip%d_%d"), i, s));
			L->SetupAttachment(Root); L->SetMobility(EComponentMobility::Movable); L->RegisterComponent();
			L->SetWorldLocationAndRotation(O + FVector(i * 1000.f + 300, s * 1400.f, HZ - 30), FRotator(-90, 0, 0));
			L->SetSourceWidth(2200); L->SetSourceHeight(60); L->SetIntensity(60000.f); L->SetLightColor(Warm); L->SetAttenuationRadius(4200);
			L->SetCastShadows(true); L->SetBarnDoorAngle(70.f);
		}
	}
	// Window wall at +X: chrome mullion grid over a full-height glass pane onto the planet.
	Box(Glass, FVector(HX + 10, 0, HZ / 2), FVector(10, 2 * HY, HZ), FRotator::ZeroRotator, false);
	for (int32 i = -6; i <= 6; ++i) Box(Chrome, FVector(HX, i * 500.f, HZ / 2), FVector(40, 30, HZ));
	for (int32 k = 1; k <= 3; ++k) Box(Chrome, FVector(HX, 0, k * 500.f), FVector(40, 2 * HY, 24));
	// Off-world view: emissive gas-giant + ring, starfield shell (instanced).
	Part(SphereMesh, Emit(Accent * 0.6f + Mag * 0.15f, 1.6f), FVector(90000, -15000, 9000), FRotator(0, 0, 0), FVector(420.f), false);
	Part(CylinderMesh, Emit(Amber, 0.5f), FVector(90000, -15000, 9000), FRotator(18, 0, 64), FVector(900.f, 900.f, 0.4f), false);
	{
		UInstancedStaticMeshComponent* Stars = NewObject<UInstancedStaticMeshComponent>(Bay, TEXT("Stars"));
		Stars->SetStaticMesh(SphereMesh); Stars->SetMaterial(0, Emit(FLinearColor::White, 6.f)); Stars->SetMobility(EComponentMobility::Static);
		Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision); Stars->SetCastShadow(false); Stars->SetupAttachment(Root); Stars->RegisterComponent();
		for (int32 i = 0; i < 2500; ++i)
		{
			const FVector D = FVector(Rng.FRandRange(0.25f, 1.f), Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-0.6f, 1.f)).GetSafeNormal();
			Stars->AddInstance(FTransform(FRotator::ZeroRotator, O + D * 160000.f, FVector(Rng.FRandRange(0.6f, 2.2f))), true);
		}
	}
	// Catwalk along -Y wall at 9 m with glass railing panels and chrome handrail.
	const float CW = 900;
	Box(Gun, FVector(0, -HY + 260, CW), FVector(2 * HX - 400, 420, 30));
	for (int32 i = -20; i <= 20; ++i) Box(Glass, FVector(i * 210.f, -HY + 470, CW + 70), FVector(200, 6, 110), FRotator::ZeroRotator, false);
	Part(CylinderMesh, Chrome, FVector(0, -HY + 470, CW + 130), FRotator(90, 0, 0), FVector(0.06f, 0.06f, (2 * HX - 400) / 100.f));
	for (int32 i = -8; i <= 8; ++i) Part(CylinderMesh, Chrome, FVector(i * 520.f, -HY + 470, CW / 2), FRotator::ZeroRotator, FVector(0.18f, 0.18f, CW / 100.f));
	// Glass partitions (chrome-framed) zoning the bay.
	for (int32 i = 0; i < 6; ++i)
	{
		const FVector C(-3600 + i * 1450.f, (i % 2 ? 1 : -1) * 2400.f, 450);
		const FRotator R(0, i % 2 ? 20.f : -20.f, 0);
		Box(Glass, C, FVector(900, 8, 900), R, false);
		Box(Chrome, C + R.RotateVector(FVector(0, 0, 455)), FVector(910, 20, 14), R);
		for (int32 s = -1; s <= 1; s += 2) Box(Chrome, C + R.RotateVector(FVector(s * 455.f, 0, 0)), FVector(16, 20, 900), R);
	}
	// Holographic signage: stacked translucent glyph bars, flickering (Time-driven, deterministic per frame time).
	const FLinearColor HoloC[] = { Cyan, Mag, Amber };
	for (int32 h = 0; h < 5; ++h)
	{
		const FVector C(-3800 + h * 1800.f, HY - 300, 1150 + (h % 2) * 250.f);
		UMaterialInstanceDynamic* HM = Holo(HoloC[h % 3]);
		Box(HM, C, FVector(900, 4, 420), FRotator::ZeroRotator, false);
		for (int32 g = 0; g < 6; ++g) Box(HM, C + FVector(Rng.FRandRange(-380, 380), -10, Rng.FRandRange(-170, 170)), FVector(Rng.FRandRange(60, 260), 3, Rng.FRandRange(18, 50)), FRotator::ZeroRotator, false);
	}
	// Big vertical hologram over the bay centre.
	Box(Holo(Cyan), FVector(0, 0, 1350), FVector(6, 1400, 700), FRotator(0, 0, 0), false);

	// Spinners: six parked on a ring + one on a raised pad. Wedge chassis, glass canopy, thrusters, running lights.
	const FLinearColor Livery[] = { FLinearColor(0.75f, 0.77f, 0.82f), FLinearColor(0.1f, 0.12f, 0.2f), FLinearColor(0.55f, 0.08f, 0.06f),
		FLinearColor(0.9f, 0.9f, 0.92f), FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor(0.2f, 0.25f, 0.3f), FLinearColor(0.8f, 0.6f, 0.25f) };
	for (int32 k = 0; k < 7; ++k)
	{
		const bool bPad = k == 6;
		const float A = k * 60.f + 15.f;
		const FVector C = bPad ? FVector(-3000, 0, 260) : FVector(FMath::Cos(FMath::DegreesToRadians(A)) * 1700.f, FMath::Sin(FMath::DegreesToRadians(A)) * 1000.f, 0);
		const FRotator Yaw(0, bPad ? 0.f : A + 90.f + Rng.FRandRange(-12, 12), 0);
		auto At = [&](const FVector& L) { return C + Yaw.RotateVector(L); };
		auto RotL = [&](const FRotator& R) { return (FQuat(Yaw) * FQuat(R)).Rotator(); };
		UMaterialInstanceDynamic* Body = Mat(ChromeMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Base"), Livery[k]); M->SetScalarParameterValue(TEXT("Roughness"), 0.05f + 0.04f * (k % 3)); });
		const bool bPolice = (k % 3) == 1;
		if (bPad) { Box(Gun, FVector(-3000, 0, 120), FVector(1300, 1300, 240)); Part(CylinderMesh, Emit(Amber, 6.f), FVector(-3000, 0, 242), FRotator::ZeroRotator, FVector(11.f, 11.f, 0.04f), false); }
		const float Z0 = C.Z + 150;
		Box(Body, At(FVector(0, 0, Z0 - C.Z + 60)), FVector(480, 190, 60), RotL(FRotator::ZeroRotator));                 // chassis
		Box(Body, At(FVector(300, 0, Z0 - C.Z + 45)), FVector(220, 180, 36), RotL(FRotator(-14, 0, 0)));                // wedge nose
		Box(Body, At(FVector(-210, 0, Z0 - C.Z + 110)), FVector(140, 200, 50), RotL(FRotator(8, 0, 0)));                // rear deck
		Box(Gun, At(FVector(-250, 0, Z0 - C.Z + 165)), FVector(60, 160, 50), RotL(FRotator(25, 0, 0)));                 // spoiler fin
		Part(SphereMesh, Canopy, At(FVector(40, 0, Z0 - C.Z + 105)), RotL(FRotator(-4, 0, 0)), FVector(2.3f, 1.35f, 0.65f)); // glass canopy
		for (int32 s = -1; s <= 1; s += 2)
		{
			Part(CylinderMesh, Chrome, At(FVector(-60, s * 125.f, Z0 - C.Z + 40)), RotL(FRotator(90, 0, 0)), FVector(0.55f, 0.55f, 3.6f)); // thruster pods
			Part(CylinderMesh, Emit(bPolice ? Blue : Mag, 10.f), At(FVector(-242, s * 125.f, Z0 - C.Z + 40)), RotL(FRotator(90, 0, 0)), FVector(0.42f, 0.42f, 0.05f), false);
			for (int32 j = 0; j < 4; ++j) Part(SphereMesh, Emit(j == 0 ? Warm : Red, 14.f), At(FVector(200 - j * 120.f, s * 97.f, Z0 - C.Z + 70)), FRotator::ZeroRotator, FVector(0.09f), false);
			for (int32 f = -1; f <= 1; f += 2) Part(CylinderMesh, Gun, At(FVector(f * 170.f, s * 80.f, (Z0 - C.Z) / 2 + 15)), FRotator::ZeroRotator, FVector(0.08f, 0.08f, (Z0 - C.Z) / 100.f));
		}
		Box(Emit(Warm, 18.f), At(FVector(408, 0, Z0 - C.Z + 52)), FVector(8, 150, 8), RotL(FRotator(-14, 0, 0)), false);       // headlight bar
		if (bPolice) for (int32 s = -1; s <= 1; s += 2) Box(Emit(s < 0 ? Red : Blue, 20.f), At(FVector(-120, s * 40.f, Z0 - C.Z + 142)), FVector(50, 38, 12), RotL(FRotator::ZeroRotator), false);
		UPointLightComponent* G = NewObject<UPointLightComponent>(Bay, *FString::Printf(TEXT("SpinnerGlow%d"), k));
		G->SetupAttachment(Root); G->SetMobility(EComponentMobility::Movable); G->RegisterComponent();
		G->SetWorldLocation(O + At(FVector(0, 0, 40))); G->SetIntensity(12000.f); G->SetAttenuationRadius(900); G->SetLightColor(bPolice ? Blue : Mag); G->SetCastShadows(true);
	}
	// Low fill lights (no shadows) so the bay never reads pitch black between strips.
	for (int32 i = 0; i < 6; ++i)
	{
		UPointLightComponent* Fl = NewObject<UPointLightComponent>(Bay, *FString::Printf(TEXT("Fill%d"), i));
		Fl->SetupAttachment(Root); Fl->SetMobility(EComponentMobility::Movable); Fl->RegisterComponent();
		Fl->SetWorldLocation(O + FVector(-3000 + (i % 3) * 3000.f, (i < 3 ? -1 : 1) * 1500.f, 1400)); Fl->SetIntensity(25000.f); Fl->SetAttenuationRadius(3800);
		Fl->SetLightColor(i % 2 ? Accent : Warm); Fl->SetCastShadows(false);
	}
	// Hangar haze: own height fog at floor level (volumetric) + a local fog pocket in the middle.
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform(O), P);
	UExponentialHeightFogComponent* F = Fog->GetComponent();
	F->SetFogDensity(0.004f); F->SetFogHeightFalloff(0.8f); F->SetFogInscatteringColor(Accent * 0.02f);
	F->SetVolumetricFog(true); F->SetVolumetricFogExtinctionScale(0.25f); F->SetVolumetricFogScatteringDistribution(0.6f); F->SetVolumetricFogAlbedo(FColor(200, 210, 230));
	ALocalFogVolume* LF = W->SpawnActor<ALocalFogVolume>(ALocalFogVolume::StaticClass(), FTransform(FRotator::ZeroRotator, O + FVector(0, 0, 300), FVector(40.f, 28.f, 6.f)), P);
	if (ULocalFogVolumeComponent* LC = LF->FindComponentByClass<ULocalFogVolumeComponent>()) { LC->SetRadialFogExtinction(0.04f); LC->SetHeightFogExtinction(0.12f); LC->SetHeightFogFalloff(300.f); LC->SetFogAlbedo(FLinearColor(0.7f, 0.75f, 0.85f)); }
	UE_LOG(LogTemp, Log, TEXT("OmaBench T2 hangar: %d components"), NComp);
}

// 60 s deterministic orbit + dolly: sweeps past the parked ring of spinners, climbs toward the
// catwalk, then pushes in on the pad spinner and finishes framed against the window.
void ABenchGameMode::UpdateHangarCamera(double T)
{
	if (!Camera) return;
	const double u = FMath::Clamp(T / 60.0, 0.0, 1.0);
	const double s = u * u * (3 - 2 * u) * 0.35 + u * 0.65;               // gentle ease, monotonic
	const double A = PI * (-0.15 + 1.55 * s);
	const double Rx = 3500 - 1300 * s, Ry = 2350 - 700 * s;
	const FVector Pos = HangarOrigin + FVector(Rx * FMath::Cos(A), Ry * FMath::Sin(A), 420 + 380 * FMath::Sin(PI * 2 * u * 1.5) * (1 - 0.4 * u) + 250 * u);
	const double B = A + 0.75;
	const FVector Look = HangarOrigin + FVector(1100 * FMath::Cos(B), 800 * FMath::Sin(B), 180 + 120 * FMath::Sin(PI * 2 * u));
	FRotator R = (Look - Pos).Rotation(); R.Roll = 4.f * FMath::Sin(PI * 2 * u);
	Camera->SetActorLocationAndRotation(Pos, R);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
}

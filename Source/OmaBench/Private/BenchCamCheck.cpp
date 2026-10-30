// Camera-path clearance check (-campathcheck[=cm]): for every selected test, enables query collision on all
// static scene geometry visible in that test, samples the camera path every 10 ms over the 60 s loop,
// sweeps a sphere of the clearance radius between samples and measures the true minimum clearance.
// Logs "CAMCHECK <Test> PASS|FAIL min=<cm> at t=<s> near=<component>" and exits 0 (all pass) or 3.
#include "BenchGameMode.h"
#include "Camera/CameraActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCamCheck, Log, All);

int32 ABenchGameMode::RunCamPathCheck(float Clearance)
{
	UWorld* W = GetWorld(); int32 Fails = 0;
	for (int32 i = 0; i < TestIds.Num(); ++i)
	{
		ActivateTest(i); CurrentTest = i;
		const FString Id = TestIds[i];
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It; const bool bUse = A != Camera && !A->IsHidden();
			for (UActorComponent* AC : A->GetComponents()) if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(AC))
			{
				const bool bOn = bUse && P->GetFName() != TEXT("SkyDome") && P->IsVisible();
				P->SetCollisionResponseToAllChannels(ECR_Block);
				P->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			}
		}
		const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("camtrace"));
		if (bTrace && Id == TEXT("T2"))
			for (AActor* A : T2Actors) if (A) for (UActorComponent* AC : A->GetComponents()) if (UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(AC))
			{ const FBox B = SM->Bounds.GetBox(); const FVector O(RoofCentre.X, RoofCentre.Y, RoofTop);
			  if (B.Max.Z > RoofTop - 100 && B.Min.Z < RoofTop + 3000 && FMath::Abs(B.GetCenter().X - O.X) < 4000 && FMath::Abs(B.GetCenter().Y - O.Y) < 4000)
				UE_LOG(LogCamCheck, Display, TEXT("CAMPROP %s %s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) mob=%d"), *SM->GetName(), SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : TEXT("-"),
					B.Min.X - O.X, B.Min.Y - O.Y, B.Min.Z - O.Z, B.Max.X - O.X, B.Max.Y - O.Y, B.Max.Z - O.Z, (int)SM->Mobility); }
		auto Eval = [&](double T) { if (Id == TEXT("T2")) UpdateSkyline(T); else if (Id == TEXT("T3")) UpdateDownpour(T); else UpdateCamera(T); return Camera->GetActorLocation(); };
		FCollisionQueryParams Q(TEXT("CamCheck"), true); Q.AddIgnoredActor(Camera);
		auto Clear = [&](const FVector& P) {   // binary search the largest free radius (cm), capped at 2000
			float Lo = 0, Hi = 2000;
			if (!W->OverlapAnyTestByChannel(P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Hi), Q)) return Hi;
			for (int32 n = 0; n < 12; ++n) { const float M = 0.5f * (Lo + Hi); (W->OverlapAnyTestByChannel(P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(M), Q) ? Hi : Lo) = M; }
			return Lo; };
		if (bTrace && Id == TEXT("T3")) for (int32 y = 7000; y <= 8000; y += 100) { FString L; for (int32 x = -700; x <= 700; x += 100) L += FString::Printf(TEXT("%5.0f"), Clear(FVector(x, y, 220))); UE_LOG(LogCamCheck, Display, TEXT("CAMPROBE y=%d %s"), y, *L); }
		float MinC = 1e9f; double MinT = 0; FString Near = TEXT("-"); int32 Hits = 0; double FirstHit = -1;
		FVector Prev = Eval(0);
		for (int32 s = 0; s <= 6000; ++s)
		{
			const double T = s * 0.01; const FVector P = Eval(T);
			FHitResult H;
			if (s > 0 && W->SweepSingleByChannel(H, Prev, P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Clearance), Q)) { ++Hits; if (FirstHit < 0) FirstHit = T; }
			if (s % 5 == 0 || H.bBlockingHit)
			{
				const float C = Clear(P);
				if (C < MinC)
				{
					MinC = C; MinT = T; TArray<FOverlapResult> Os;
					if (W->OverlapMultiByChannel(Os, P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(C + 20), Q) && Os.Num() && Os[0].GetComponent())
						Near = Os[0].GetActor() ? FString::Printf(TEXT("%s.%s"), *Os[0].GetActor()->GetName(), *Os[0].GetComponent()->GetName()) : Os[0].GetComponent()->GetName();
					if (Os.Num() && Os[0].GetComponent()) if (UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Os[0].GetComponent())) if (SM->GetStaticMesh()) Near += TEXT("[") + SM->GetStaticMesh()->GetName() + TEXT("]");
				}
			}
			if (bTrace && s % 25 == 0) { const float C = Clear(P); const FVector L = Id == TEXT("T2") ? P - FVector(RoofCentre.X, RoofCentre.Y, RoofTop) : P;
				UE_LOG(LogCamCheck, Display, TEXT("CAMTRACE %s t=%.2f pos=(%.0f,%.0f,%.0f) clear=%.0f"), *Id, T, L.X, L.Y, L.Z, C); }
			Prev = P;
		}
		const bool bOk = Hits == 0 && MinC >= Clearance;
		Fails += !bOk;
		UE_LOG(LogCamCheck, Display, TEXT("CAMCHECK %s %s min=%.0fcm at t=%.2fs near=%s sweepHits=%d firstHit=%.2fs (clearance %.0fcm)"),
			*Id, bOk ? TEXT("PASS") : TEXT("FAIL"), MinC, MinT, *Near, Hits, FirstHit, Clearance);
		for (TActorIterator<AActor> It(W); It; ++It) for (UActorComponent* AC : It->GetComponents()) if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(AC)) P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UE_LOG(LogCamCheck, Display, TEXT("CAMCHECK DONE %s (%d failing)"), Fails ? TEXT("FAIL") : TEXT("PASS"), Fails);
	return Fails ? 3 : 0;
}

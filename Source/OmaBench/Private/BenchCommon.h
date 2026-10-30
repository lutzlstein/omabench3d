// Shared scene helpers for the test builders (City Night / Skyline / Downpour).
#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

namespace OmaBench
{
	inline UStaticMesh* LoadMesh(const TCHAR* Name)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/OmaBench/Meshes/%s.%s"), Name, Name));
		if (!M) UE_LOG(LogTemp, Error, TEXT("OmaBench: missing mesh %s (run art/build.sh)"), Name);
		return M;
	}
	// CC0 world-projected material (Scripts/make_cc0_materials.py); falls back if the art step has not run.
	inline UMaterialInterface* LoadCC0(const TCHAR* Name, UMaterialInterface* Fallback)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/OmaBench/CC0/%s.%s"), Name, Name));
		return M ? M : Fallback;
	}
	// Bind materials by Blender slot name (slot "Paint.001" -> "Paint").
	inline void BindSlots(UStaticMeshComponent* C, const TMap<FName, UMaterialInterface*>& Slots)
	{
		const UStaticMesh* M = C->GetStaticMesh(); if (!M) return;
		const TArray<FStaticMaterial>& SM = M->GetStaticMaterials();
		for (int32 i = 0; i < SM.Num(); ++i)
		{
			FString N = SM[i].MaterialSlotName.ToString(); int32 Dot; if (N.FindChar(TEXT('.'), Dot)) N.LeftInline(Dot);
			if (UMaterialInterface* const* Mat = Slots.Find(FName(*N))) C->SetMaterial(i, *Mat);
		}
	}
	inline double Smooth(double x) { x = FMath::Clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }
}

#pragma once
#include "Commandlets/Commandlet.h"
#include "OmaFxCommandlet.generated.h"
// Builds the T3 "Downpour" Niagara GPU systems from code (no hand-authored assets).
UCLASS()
class UOmaFxCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};

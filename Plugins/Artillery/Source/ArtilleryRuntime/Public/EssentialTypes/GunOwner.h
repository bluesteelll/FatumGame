#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "GunOwner.generated.h"

class UFireControlMachine;

UINTERFACE()
class UArtilleryFireControlInterface : public UInterface
{
	GENERATED_BODY()
};

class IArtilleryFireControlInterface
{
	GENERATED_BODY()

public:
	virtual UFireControlMachine* GetFireControlMachine() {return nullptr;};
};

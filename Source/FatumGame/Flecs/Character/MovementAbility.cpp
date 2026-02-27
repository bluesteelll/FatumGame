// Movement ability base class implementation.

#include "MovementAbility.h"
#include "FatumMovementComponent.h"

void UMovementAbility::Initialize(UFatumMovementComponent* InOwner)
{
	check(InOwner);
	OwnerCMC = InOwner;
}

// FlecsArtillerySubsystem - Collision Pair Systems (orchestrator)
// Delegates to domain-specific sub-methods.

#include "FlecsArtillerySubsystem.h"

void UFlecsArtillerySubsystem::SetupCollisionSystems()
{
	SetupPenetrationSystem();            // Penetration (MUST run before Damage)
	SetupDamageCollisionSystems();       // Weapon domain
	SetupPickupCollisionSystems();       // Item domain
	SetupDestructibleCollisionSystems(); // Destructible domain
}

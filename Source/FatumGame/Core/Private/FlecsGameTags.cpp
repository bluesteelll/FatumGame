// Game-specific Flecs tags implementation.
// Tags are zero-size - no registration macros needed.
// They are registered in SetupFlecsSystems() using World.component<>().

#include "FlecsGameTags.h"

// ═══════════════════════════════════════════════════════════════
// ITEM GAMEPLAY TAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════

UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Weapon_Ranged, "Item.Weapon.Ranged");
UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Weapon_Melee,  "Item.Weapon.Melee");

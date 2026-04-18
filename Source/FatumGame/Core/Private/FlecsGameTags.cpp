// Game-specific Flecs tags implementation.
// Tags are zero-size - no registration macros needed.
// They are registered in SetupFlecsSystems() using World.component<>().

#include "FlecsGameTags.h"

// ═══════════════════════════════════════════════════════════════
// ITEM GAMEPLAY TAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════

UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Weapon_Ranged, "Item.Weapon.Ranged");
UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Weapon_Melee,  "Item.Weapon.Melee");

// ═══════════════════════════════════════════════════════════════
// CRAFTING STATION TAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════

UE_DEFINE_GAMEPLAY_TAG(Tag_Station_Smelter,  "Station.Smelter");
UE_DEFINE_GAMEPLAY_TAG(Tag_Station_Press,    "Station.Press");
UE_DEFINE_GAMEPLAY_TAG(Tag_Station_Forge,    "Station.Forge");
UE_DEFINE_GAMEPLAY_TAG(Tag_Station_Alchemy,  "Station.Alchemy");
UE_DEFINE_GAMEPLAY_TAG(Tag_Station_Generic,  "Station.Generic");

// ═══════════════════════════════════════════════════════════════
// FUEL ITEM TAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════

UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Fuel_Coal,         "Item.Fuel.Coal");
UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Fuel_ElectricCell, "Item.Fuel.ElectricCell");
UE_DEFINE_GAMEPLAY_TAG(Tag_Item_Fuel_Mana,         "Item.Fuel.Mana");

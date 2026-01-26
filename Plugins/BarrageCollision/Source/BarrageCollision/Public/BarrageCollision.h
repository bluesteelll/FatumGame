// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Barrage Collision - Convenience header

#pragma once

/**
 * Barrage Collision Module
 *
 * Integrates Barrage physics collision events with Phosphorus event dispatch.
 *
 * Include this header for full access to:
 * - UBarrageCollisionSubsystem (world subsystem)
 * - FBarrageCollisionPayload (collision event data)
 * - FBarrageCollisionDispatcher (Phosphorus dispatcher type)
 * - FBPCollisionHandler (Blueprint delegate)
 * - FNativeCollisionHandler (C++ handler type)
 *
 * Quick Start:
 *
 *   // Get subsystem
 *   auto* Collision = UBarrageCollisionSubsystem::Get(GetWorld());
 *
 *   // Register type handler (Blueprint)
 *   Collision->RegisterTypeHandler(EEntityType::Projectile, EEntityType::Actor, MyHandler);
 *
 *   // Register type handler (C++)
 *   Collision->GetDispatcher().RegisterTypeHandler(
 *       EEntityType::Projectile,
 *       EEntityType::Actor,
 *       FNativeCollisionHandler::CreateLambda([](const FBarrageCollisionPayload& P) {
 *           // Handle collision
 *           return true; // consumed
 *       })
 *   );
 *
 *   // Register tag handler
 *   Collision->RegisterTag(TAG_Enemy);
 *   Collision->RegisterTagHandler(TAG_Enemy, TAG_Weapon, EnemyWeaponHandler);
 */

#include "BarrageCollisionModule.h"
#include "BarrageCollisionTypes.h"
#include "BarrageCollisionSubsystem.h"

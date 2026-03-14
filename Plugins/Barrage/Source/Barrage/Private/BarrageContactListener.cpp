#include "BarrageContactListener.h"
#include "BarrageDispatch.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "BarrageContactEvent.h"
#include "FWorldSimOwner.h"

using namespace JPH;

	ValidateResult BarrageContactListener::OnContactValidate(const Body& inBody1, const Body& inBody2, RVec3Arg inBaseOffset,
												 const CollideShapeResult& inCollisionResult)
	{
		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void BarrageContactListener::OnContactAdded(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold,
								ContactSettings& ioSettings)
	{
		if (UBarrageDispatch::SelfPtr)
		{
			UBarrageDispatch::SelfPtr->HandleContactAdded(inBody1, inBody2, inManifold, ioSettings);
		}
	}

	void BarrageContactListener::OnContactPersisted(const Body& inBody1, const Body& inBody2, const ContactManifold& inManifold,
									ContactSettings& ioSettings)
	{
		// TODO: Honestly, this event fires way too often and we don't really need it so... let's just not for now

		/**
		if (BarrageDispatch.IsValid())
		{
			BarrageDispatch->HandleContactPersisted(inBody1, inBody2, inManifold, ioSettings);
		}
		**/
	}

//TODO: This is a JANKY hack for character collisions and basically doesn't include enough info to really get
//fine grained about stuff.
void BarrageContactListener::OnContactAdded(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
	const JPH::SubShapeID& inSubShapeID2, JPH::RVec3Arg inContactPosition, JPH::Vec3Arg inContactNormal,
	JPH::CharacterContactSettings& ioSettings)
{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Report Contact");
		if (UBarrageDispatch::SelfPtr)
		{
			auto Ent1 = BarrageContactEntity(
				UBarrageDispatch::SelfPtr->GenerateBarrageKeyFromBodyId(inCharacter->GetInnerBodyID())
					);

			auto Ent2 = BarrageContactEntity(UBarrageDispatch::SelfPtr->GenerateBarrageKeyFromBodyId(inBodyID2));

			// Read actual object layer from Jolt body interface (BarrageContactEntity defaults to NUM_LAYERS).
			uint8 ActualLayer = Layers::NON_MOVING;
			if (UBarrageDispatch::SelfPtr->JoltGameSim && UBarrageDispatch::SelfPtr->JoltGameSim->body_interface)
			{
				ActualLayer = static_cast<uint8>(
					UBarrageDispatch::SelfPtr->JoltGameSim->body_interface->GetObjectLayer(inBodyID2));
			}

			if (ActualLayer == Layers::NON_MOVING)
			{
				// Static world: can push character (ground support), cannot receive impulses.
				ioSettings.mCanReceiveImpulses = false;
			}
			else
			{
				// Dynamic objects: never push the character via contact.
				// Explosions/grenades use OtherForce -> mForcesUpdate instead.
				ioSettings.mCanPushCharacter = false;

				// Impulse direction check via contact normal:
				// Normal points FROM character TO body.
				// Standing on object: normal.Y < -0.5 (pointing down) → don't push object down (prevents jitter).
				// Pushing into object: normal mostly horizontal → push normally.
				if (inContactNormal.GetY() < -0.5f)
				{
					ioSettings.mCanReceiveImpulses = false; // standing ON object — no downward impulse
				}
			}
			UBarrageDispatch::SelfPtr->HandleContactAdded(Ent1, Ent2);
		}
}

//TODO: this needs replaced when we go to multiplayer.
void BarrageContactListener::OnCharacterContactAdded(const JPH::CharacterVirtual* inCharacter,
	const JPH::CharacterVirtual* inOtherCharacter, const JPH::SubShapeID& inSubShapeID2,
	JPH::RVec3Arg inContactPosition, JPH::Vec3Arg inContactNormal, JPH::CharacterContactSettings& ioSettings)
{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Report Contact");
		if (UBarrageDispatch::SelfPtr)
		{
			auto Ent1 = BarrageContactEntity(UBarrageDispatch::SelfPtr->GenerateBarrageKeyFromBodyId(inCharacter->GetInnerBodyID()));
			
			auto Ent2 = BarrageContactEntity(UBarrageDispatch::SelfPtr->GenerateBarrageKeyFromBodyId(inOtherCharacter->GetInnerBodyID()));
			UBarrageDispatch::SelfPtr->HandleContactAdded(Ent1, Ent2);
		}  
}

void BarrageContactListener::OnContactRemoved(const SubShapeIDPair& inSubShapePair)
	{
		if (UBarrageDispatch::SelfPtr)
		{
			UBarrageDispatch::SelfPtr->HandleContactRemoved(inSubShapePair);
		}
	}

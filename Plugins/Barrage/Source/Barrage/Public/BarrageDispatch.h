// ReSharper disable CppMemberFunctionMayBeConst
#pragma once

#include "SkeletonTypes.h"

#include "CoreMinimal.h"
#include "BarrageContactEvent.h"
#include "Subsystems/WorldSubsystem.h"
#include "FBarrageKey.h"
#include "FBConstraintParams.h"
#include "Chaos/Particles.h"
#include "CapsuleTypes.h"
#include "FBarragePrimitive.h"
#include "FBPhysicsInput.h"
#include "Containers/CircularQueue.h"
#include "FBShapeParams.h"
#include "KeyedConcept.h"
#include "ORDIN.h"
#include "TransformDispatch.h"
#include "BarrageDispatch.generated.h"

#ifndef HERTZ_OF_BARRAGE
#define HERTZ_OF_BARRAGE 128.0f
#endif

static constexpr uint32 MAX_FOUND_OBJECTS = 1024;

class BARRAGE_API FBarrageBounder
{
	friend class FBBoxParams;
	friend class FBSphereParams;
	friend class FBCapParams;
	//convert from UE to Jolt without exposing the jolt types or coordinates.
public:
	static FBBoxParams GenerateBoxBounds(
		const FVector3d& point,
		double xDiam, double yDiam, double zDiam,
		const FVector3d& OffsetCenterToMatchBoundedShape = FVector::Zero(),
		FMassByCategory::BMassCategories MyMassClass = FMassByCategory::BMassCategories::MostEnemies);
	static FBSphereParams GenerateSphereBounds(const FVector3d& point, double radius);
	static FBCapParams GenerateCapsuleBounds(const UE::Geometry::FCapsule3d& Capsule);
	static FBCapParams GenerateCapsuleBounds(FVector Center, float Radius, float Height, FMassByCategory::BMassCategories Mass, FVector3f Offsets);
	static FBCharParams GenerateCharacterBounds(const FVector3d& point, double radius, double extent, double speed);
};

struct BarrageContactEvent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBarrageContactAdded, const BarrageContactEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBarrageContactPersisted, const BarrageContactEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBarrageContactRemoved, const BarrageContactEvent&);
constexpr int ALLOWED_THREADS_FOR_BARRAGE_PHYSICS = 64;
//if we could make a promise about when threads are allocated, we could probably get rid of this
//since the accumulator is in the world subsystem and so gets cleared when the world spins down.
//that would mean that we could add all the threads, then copy the state from the volatile array to a
//fixed read-only hash table during begin play. this is already complicated though, and let's see if we like it
//before we invest more time in it. I had a migraine when I wrote this, by way of explanation --J

	//hi, static actually is per TLU. this has... exotic behavior in our use cases! 
	inline thread_local extern  int32 MyBARRAGEIndex = ALLOWED_THREADS_FOR_BARRAGE_PHYSICS + 1;

	inline thread_local extern  int32 MyWORKERIndex = ALLOWED_THREADS_FOR_BARRAGE_PHYSICS + 1;



UCLASS()
class BARRAGE_API UBarrageDispatch : public UTickableWorldSubsystem, public ISkeletonLord, public ICanReady
{
	GENERATED_BODY()
	
	friend class FWorldSimOwner;

public:
	static inline UBarrageDispatch* SelfPtr = nullptr;
	constexpr static int OrdinateSeqKey = ORDIN::LastSubstrateKey;
	virtual bool RegistrationImplementation() override;
	void GrantWorkerFeed(int MyThreadIndex);
	static constexpr float TickRateInDelta = 1.0f / HERTZ_OF_BARRAGE;
	int32 ThreadAccTicker = 0;
	TSharedPtr<TransformUpdatesForGameThread> GameTransformPump;
	TSharedPtr<TCircularQueue<BarrageContactEvent>> ContactEventPump;
	 //this value indicates you have none.
	mutable FCriticalSection GrowOnlyAccLock;
	int32 WorkerThreadAccTicker = 0;
	mutable FCriticalSection MultiAccLock;

	// Why would I do it this way? It's fast and easy to debug, and we will probably need to force a thread
	// order for determinism. this ensures there's a call point where we can institute that.
	void GrantClientFeed();
	UBarrageDispatch();
	
	virtual ~UBarrageDispatch() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	
	virtual void SphereCast(double Radius, double Distance, FVector3d CastFrom, FVector3d Direction, TSharedPtr<FHitResult> OutHit, const JPH::BroadPhaseLayerFilter& BroadPhaseFilter, const JPH::ObjectLayerFilter& ObjectFilter, const JPH::BodyFilter& BodiesFilter, uint64_t timestamp = 0);
	virtual void SphereSearch(FBarrageKey ShapeSource, FVector3d Location, double Radius, const JPH::BroadPhaseLayerFilter& BroadPhaseFilter, const JPH::ObjectLayerFilter& ObjectFilter, const JPH::BodyFilter& BodiesFilter, uint32* OutFoundObjectCount, TArray<uint32>& OutFoundObjects);

	virtual void CastRay(FVector3d CastFrom, FVector3d Direction, const JPH::BroadPhaseLayerFilter& BroadPhaseFilter, const JPH::ObjectLayerFilter& ObjectFilter, const JPH::BodyFilter& BodiesFilter, TSharedPtr<FHitResult> OutHit);
	
	//and viola [sic] actually pretty elegant even without type polymorphism by using overloading polymorphism.
	FBLet CreatePrimitive(FBBoxParams& Definition, FSkeletonKey Outkey, uint16 Layer, bool IsSensor = false, bool forceDynamic = false, bool isMovable = true, float Friction = 0.5f, float Restitution = 0.3f, float LinearDamping = 0.05f);
	FBLet CreatePrimitive(FBCapParams& Definition, FSkeletonKey Outkey, uint16 Layer, bool IsSensor = false, bool forceDynamic = false, bool isMovable = true);
	FBLet CreatePrimitive(FBCharParams& Definition, FSkeletonKey Outkey, uint16 Layer);
	FBLet CreatePrimitive(FBSphereParams& Definition, FSkeletonKey OutKey, uint16 Layer, bool IsSensor = false);
	FBLet CreateProjectile(FBBoxParams& Definition, FSkeletonKey OutKey, uint16_t Layer);

	// Create a bouncing sphere projectile - for ricocheting bullets etc.
	// Uses Jolt physics for realistic bounces (restitution controls elasticity)
	FBLet CreateBouncingSphere(FBSphereParams& Definition, FSkeletonKey OutKey, uint16_t Layer, float Restitution = 0.8f, float Friction = 0.2f, float LinearDamping = 0.0f);
	FBLet LoadComplexStaticMesh(FBTransform& MeshTransform, const UStaticMeshComponent* StaticMeshComponent, FSkeletonKey OutKey, bool IsSensor = false);
	FBLet LoadEnemyHitboxFromStaticMesh(FBTransform& MeshTransform, const UStaticMeshComponent* StaticMeshComponent, FSkeletonKey OutKey, bool IsSensor = false, bool UseRawMeshForCollision = false, FVector CenterOfMassTranslation = {0,0,0});
	FBLet GetShapeRef(FBarrageKey Existing) const;
	FBLet GetShapeRef(FSkeletonKey Existing) const;
	void FinalizeReleasePrimitive(FBarrageKey BarrageKey);

	/**
	 * Change a body's physics layer. Use to disable collision without destroying the body.
	 * Moving to Layers::DEBRIS effectively disables gameplay collision while letting
	 * tombstone handle safe destruction later.
	 */
	void SetBodyObjectLayer(FBarrageKey BarrageKey, uint8 NewLayer);

	/** Synchronously set body position (NOT queued). Use from sim thread. */
	void SetBodyPositionDirect(FBarrageKey BarrageKey, const FVector& Position, bool bActivate = true);

	/** Synchronously set body rotation (NOT queued). Use from sim thread. */
	void SetBodyRotationDirect(FBarrageKey BarrageKey, const FQuat& Rotation, bool bActivate = true);

	/** Synchronously change body motion type (Static/Dynamic/Kinematic). Use from sim thread. */
	void SetBodyMotionType(FBarrageKey BarrageKey, JPH::EMotionType MotionType, bool bActivate = true);

	/** Synchronously override a body's mass (kg). Recalculates inertia from shape. Use from sim thread. */
	void SetBodyMass(FBarrageKey BarrageKey, float MassKg);

	/** Synchronously zero both linear and angular velocity. Use from sim thread for pool body reset. */
	void ResetBodyVelocities(FBarrageKey BarrageKey);

	/** Get angular velocity (Jolt rad/s, Jolt axes). Use from sim thread. */
	FVector3d GetBodyAngularVelocity(FBarrageKey BarrageKey);

	/** Set angular velocity (Jolt rad/s, Jolt axes). Use from sim thread. */
	void SetBodyAngularVelocity(FBarrageKey BarrageKey, const FVector3d& AngVel);

	/** Synchronously apply impulse (kg·cm/s in UE coordinates) to a body's center of mass.
	 *  Internally converts to Jolt units and calls body_interface->AddImpulse().
	 *  Safe to call from Flecs worker threads during progress() (i.e., AFTER StepWorld completes).
	 *  Do NOT call from inside Jolt job threads while PhysicsSystem::Update() is executing. */
	void AddBodyImpulse(FBarrageKey BarrageKey, FVector ImpulseUE);

	// Wake up all sleeping bodies in a given area - useful when removing support from stacked objects
	void ActivateBodiesInArea(const FVector3d& Center, double HalfExtent);

	// Wake up all bodies touching/near a specific body using its actual bounding box (PREFERRED)
	// This is the PROPER way per Jolt documentation - more efficient than arbitrary area
	void ActivateBodiesAroundBody(FBarrageKey BodyKey, float ExpansionMeters = 0.1f);

	// ============================================================
	// Constraint API - Create breakable connections between bodies
	// ============================================================

	/**
	 * Get the constraint system for advanced constraint management.
	 * @return Pointer to constraint system, or nullptr if not initialized
	 */
	class FBarrageConstraintSystem* GetConstraintSystem();
	const class FBarrageConstraintSystem* GetConstraintSystem() const;

	/**
	 * Create a fixed (welded) constraint between two bodies.
	 * Bodies will move as if welded together until the constraint breaks.
	 *
	 * @param Body1 First body key
	 * @param Body2 Second body key (or invalid for world constraint)
	 * @param BreakForce Force in Newtons that will break the constraint (0 = unbreakable)
	 * @param BreakTorque Torque in Nm that will break the constraint (0 = unbreakable)
	 * @return Constraint key for later management
	 */
	FBarrageConstraintKey CreateFixedConstraint(FBarrageKey Body1, FBarrageKey Body2,
												 float BreakForce = 0.0f, float BreakTorque = 0.0f);

	/**
	 * Create a hinge constraint between two bodies.
	 * Bodies can rotate around the specified axis.
	 *
	 * @param Body1 First body key
	 * @param Body2 Second body key
	 * @param WorldAnchor World position of the hinge point
	 * @param HingeAxis Axis of rotation (world space)
	 * @param BreakForce Force in Newtons that will break the constraint
	 * @return Constraint key
	 */
	FBarrageConstraintKey CreateHingeConstraint(FBarrageKey Body1, FBarrageKey Body2,
												 FVector WorldAnchor, FVector HingeAxis,
												 float BreakForce = 0.0f);

	/**
	 * Create a distance constraint (rope/spring) between two bodies.
	 *
	 * @param Body1 First body key
	 * @param Body2 Second body key
	 * @param MinDistance Minimum distance (0 = no minimum)
	 * @param MaxDistance Maximum distance (0 = auto-detect from current positions)
	 * @param BreakForce Force that will break the rope
	 * @param SpringFrequency Spring stiffness in Hz (0 = rigid, higher = stiffer)
	 * @param SpringDamping Damping ratio 0-1 (0 = bouncy, 1 = no bounce)
	 * @param bLockRotation Lock relative rotation (bodies can't rotate independently)
	 * @return Constraint key
	 */
	FBarrageConstraintKey CreateDistanceConstraint(FBarrageKey Body1, FBarrageKey Body2,
													float MinDistance = 0.0f, float MaxDistance = 0.0f,
													float BreakForce = 0.0f, float SpringFrequency = 0.0f,
													float SpringDamping = 0.5f, bool bLockRotation = false);

	// ── Runtime Motor Control ──

	/** Set motor state. 0=Off, 1=Velocity, 2=Position. */
	bool SetConstraintMotorState(FBarrageConstraintKey Key, uint8 MotorState);

	/** Set target angle for hinge Position motor (radians). */
	bool SetConstraintTargetAngle(FBarrageConstraintKey Key, float AngleRadians);

	/** Set target position for slider Position motor (Jolt meters). */
	bool SetConstraintTargetPosition(FBarrageConstraintKey Key, float PositionMeters);

	/** Configure motor spring at runtime. */
	bool SetConstraintMotorSpring(FBarrageConstraintKey Key, float Frequency, float Damping);

	/** Set motor torque limits at runtime (symmetric: -MaxTorque to +MaxTorque). */
	bool SetConstraintMotorTorqueLimits(FBarrageConstraintKey Key, float MaxTorque);

	/** Set constraint friction at runtime. Hinge: Nm, Slider: N. Wakes bodies. */
	bool SetConstraintFriction(FBarrageConstraintKey Key, float Value);

	/** Get current angle of hinge constraint (radians). */
	float GetConstraintCurrentAngle(FBarrageConstraintKey Key) const;

	/** Get current position of slider constraint (Jolt meters). */
	float GetConstraintCurrentPosition(FBarrageConstraintKey Key) const;

	// ── Angular Damping ──

	/** Set angular damping on a body. Requires body lock. */
	void SetBodyAngularDamping(FBarrageKey Key, float Damping);

	/** Change a body's shape to a capsule at runtime. Use for posture changes (stand/crouch/prone).
	 *  JoltHalfHeight and JoltRadius are in Jolt meters (use CoordinateUtils to convert).
	 *  Call from sim thread via EnqueueCommand. */
	void SetBodyCapsuleShape(FBarrageKey BarrageKey, double JoltHalfHeight, double JoltRadius);

	/** Change a CharacterVirtual's outer shape AND inner body shape to a capsule at runtime.
	 *  For posture changes on characters created via FBCharParams.
	 *  JoltHalfHeight and JoltRadius are in Jolt meters.
	 *  Call from sim thread via EnqueueCommand. */
	void SetCharacterCapsuleShape(FSkeletonKey Key, double JoltHalfHeight, double JoltRadius);

	/**
	 * Remove a constraint.
	 * @param Key The constraint to remove
	 * @return True if found and removed
	 */
	bool RemoveConstraint(FBarrageConstraintKey Key);

	/**
	 * Process breakable constraints and break any that exceed their thresholds.
	 * Call this once per physics tick if using breakable constraints.
	 * @return Number of constraints that broke
	 */
	int32 ProcessBreakableConstraints();

	//any non-zero value is the same, effectively, as a nullity for the purposes of any new operation.
	//because we can't control certain aspects of timing and because we may need to roll back, we use tombstoning
	//instead of just reference counting and deleting - this is because cases arise where there MUST be an authoritative
	//single source answer to the alive/dead question for a rigid body, but we still want all the advantages of ref counting
	//and we want to be able to revert that decision for faster rollbacks or for pooling purposes.
	constexpr static uint32 TombstoneInitialMinimum = 9 << 8;

	//don't const stuff that causes huge side effects.
	uint32 SuggestTombstone(FBLet Target) 
	{
		if (FBarragePrimitive::IsNotNull(Target))
		{
			Target->tombstone = TombstoneInitialMinimum + TombOffset;
			return Target->tombstone;
		}
		return 1;
		// one indicates that something has no remaining time to live, and is equivalent to finding a nullptr. we return if for tombstone suggestions against tombstoned or null data.
	}

	virtual TStatId GetStatId() const override;
	TSharedPtr<FWorldSimOwner> JoltGameSim;

	//StackUp should be called before StepWorld and from the same thread. anything can be done between them.
	//Returns rather than applies the FBPhysicsInputs that affect Primitives of Types: Character
	//This list may expand. Failure to handle these will result in catastrophic bugs.
	void StackUp();
	bool UpdateCharacters(TSharedPtr<TArray<FBPhysicsInput>> CharacterInputs) const;
	bool UpdateCharacter(FBPhysicsInput& CharacterInput) const;
	
	//ONLY call this from a thread OTHER than gamethread, or you will experience untold sorrow.
	void StepWorld(float InDeltaTime, uint64_t TickCount);

	//TODO: oh dear I'm doing the same thing as the TransformQueue... Also probably want to check back on this.
	bool BroadcastContactEvents() const;
	
	FOnBarrageContactAdded OnBarrageContactAddedDelegate;
	void HandleContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold,
	                        JPH::ContactSettings& ioSettings);
	void HandleContactAdded(BarrageContactEntity Ent1, BarrageContactEntity Ent2);
	void HandleContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::ContactSettings& ioSettings, FVector point);
	FOnBarrageContactPersisted OnBarrageContactPersistedDelegate;
	void HandleContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold,
	                            JPH::ContactSettings& ioSettings);
	// REMOVE EVENTS REQUIRE ADDITIONAL SPECIAL HANDLING AS THEY DO NOT HAVE ALL DATA SET
	FOnBarrageContactRemoved OnBarrageContactRemovedDelegate;
	// REMOVE EVENTS REQUIRE ADDITIONAL SPECIAL HANDLING AS THEY DO NOT HAVE ALL DATA SET
	void HandleContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) const;

	FBarrageKey GenerateBarrageKeyFromBodyId(const JPH::BodyID& Input) const;
	FBarrageKey GenerateBarrageKeyFromBodyId(const uint32 RawIndexAndSequenceNumberInput) const;

	FBarrageKey GetBarrageKeyFromFHitResult(TSharedPtr<FHitResult> HitResult) const
	{
		check(HitResult.IsValid());
		return HitResult.Get()->MyItem != JPH::BodyID::cInvalidBodyID ? GenerateBarrageKeyFromBodyId(static_cast<uint32>(HitResult.Get()->MyItem)) : 0;
	}

	JPH::DefaultBroadPhaseLayerFilter GetDefaultBroadPhaseLayerFilter(JPH::ObjectLayer inLayer) const;
	JPH::DefaultObjectLayerFilter GetDefaultLayerFilter(JPH::ObjectLayer inLayer) const;

	static JPH::SpecifiedObjectLayerFilter GetFilterForSpecificObjectLayerOnly(JPH::ObjectLayer inLayer);
	
	JPH::IgnoreSingleBodyFilter GetFilterToIgnoreSingleBody(FBarrageKey ObjectKey) const;
	JPH::IgnoreSingleBodyFilter GetFilterToIgnoreSingleBody(const FBLet& ToIgnore) const;

	int32 GetBodyCount() const
	{
		return JoltBodyLifecycleMapping ? static_cast<int32>(JoltBodyLifecycleMapping->size()) : 0;
	}

	// ============================================================
	// Key Translation API
	// ============================================================

	/**
	 * Get a FBarrageKey from a FSkeletonKey.
	 * Used by the constraint system to translate actor keys to physics body keys.
	 *
	 * @param SkeletonKey The skeleton key (e.g., from UPlayerKeyCarry)
	 * @return The corresponding FBarrageKey, or invalid key if not found
	 */
	FBarrageKey GetBarrageKeyFromSkeletonKey(FSkeletonKey SkeletonKey) const
	{
		if (!TranslationMapping || !SkeletonKey.IsValid())
		{
			return FBarrageKey();
		}

		FBarrageKey Result;
		if (TranslationMapping->find(SkeletonKey, Result))
		{
			return Result;
		}
		return FBarrageKey();
	}

	/**
	 * Check if a FSkeletonKey has a corresponding physics body.
	 *
	 * @param SkeletonKey The skeleton key to check
	 * @return True if the key maps to a physics body
	 */
	bool HasBarrageBody(FSkeletonKey SkeletonKey) const
	{
		if (!TranslationMapping || !SkeletonKey.IsValid())
		{
			return false;
		}
		return TranslationMapping->contains(SkeletonKey);
	}

private:
	TSharedPtr<KeyToFBLet> JoltBodyLifecycleMapping;
	TSharedPtr<KeyToKey> TranslationMapping;
	FBLet ManagePointers(FSkeletonKey OutKey, FBarrageKey temp, FBShape form) const;
	uint32 TombOffset = 0; //ticks up by one every world step.

	//this is a little hard to explain. so keys are inserted as 

	//clean tombs must only ever be called from step world which must only ever be called from one thread.
	//this reserves as little memory as possible, but it could be quite a lot (megs) of reserved memory if you expire
	//tons and tons of bodies in one frame. if that's a bottleneck for you, you may wish to shorten the tombstone promise
	//or optimize this for memory better. In general, Barrage trades memory for speed and elegance.
	TSharedPtr<TArray<FBLet>> Tombs[TombstoneInitialMinimum + 1];

	void CleanTombs()
	{
		//free tomb at offset - TombstoneInitialMinimum, fulfilling our promised minimum.
		TSharedPtr<TArray<FBLet>>* HoldOpen = Tombs;
		TSharedPtr<TArray<FBLet>> Mausoleum = HoldOpen[(TombOffset) % (TombstoneInitialMinimum + 1)]; //think this math is wrong.
		TSharedPtr<KeyToFBLet> HoldOpenBMap = JoltBodyLifecycleMapping;
		TSharedPtr<KeyToKey> HoldOpenTMap = TranslationMapping;
		if(Mausoleum && !Mausoleum->IsEmpty() && HoldOpenBMap && HoldOpenTMap)
		{
			for (auto Tombstone : *Mausoleum)
			{
				if (Tombstone)
				{
					JoltBodyLifecycleMapping->erase(Tombstone->KeyIntoBarrage);
					TranslationMapping->erase(Tombstone->KeyOutOfBarrage);
				}
			}
		}
		
		Mausoleum = HoldOpen[(TombOffset - TombstoneInitialMinimum) % (TombstoneInitialMinimum + 1)];
		if (Mausoleum)
		{
			Mausoleum->Empty(); //roast 'em lmao.
		}
		TombOffset = (TombOffset + 1) % (TombstoneInitialMinimum + 1);
	}

private:
	std::array<FBPhysicsInput, 32000> InternalSortableSet = {};
	std::array<JPH::BodyID, 8192> Adds;
};

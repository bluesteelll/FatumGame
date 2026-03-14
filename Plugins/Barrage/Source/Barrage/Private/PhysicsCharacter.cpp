#include "PhysicsCharacter.h"

using namespace JOLT;

JPH::BodyID FBCharacter::Create(JPH::CharacterVsCharacterCollision* CVCColliderSystem)
{
	JPH::BodyID ret = BodyID();
	// Create capsule
	if(World) 
	{
		//WorldSimOwner manages the lifecycle of the physics characters. we don't have a proper destructor in here yet, I'm just trying to get this UP for now.
		if(World)
		{
			mGravity = Vec3(0, -9.80, 0);
			// mMass controls ONLY the gravity impulse applied to ground body (line 1417 in CharacterVirtual.cpp).
			// Mass=0 disables it entirely, preventing jitter when standing on dynamic objects.
			// Lateral pushing (HandleContact) uses mMaxStrength instead, unaffected by mMass.
			mCharacterSettings.mMass = 0;
			Ref<Shape> capsule = new CapsuleShape(0.5f * mHeightStanding, mRadiusStanding);
			Ref<Shape> capsuleB = new CapsuleShape(0.5f * mHeightStanding, mRadiusStanding);
			mCharacterSettings.mEnhancedInternalEdgeRemoval = true;
			mCharacterSettings.mShape = RotatedTranslatedShapeSettings(
				Vec3(0, 0.5f * mHeightStanding + mRadiusStanding, 0), Quat::sIdentity(), capsule).Create().Get();
			// Configure supporting volume
			mCharacterSettings.mSupportingVolume = Plane(Vec3::sAxisY(), -mHeightStanding);
			mForcesUpdate = Vec3::sZero();
			// HITBOX layer: projectiles + raycasts hit the character, but inner body
			// won't physically push MOVING-layer dynamic objects during StepSimulation.
			mCharacterSettings.mInnerBodyLayer = Layers::EJoltPhysicsLayer::HITBOX;
			auto SettingsForInnerShape = RotatedTranslatedShapeSettings(
				Vec3(0, 0.5f * mHeightStanding + mRadiusStanding, 0), Quat::sIdentity(), capsuleB);
			// Accept contacts that touch the lower sphere of the capsule
			// If you want to create character WITH innerbodyshape - don't try to reduce, reuse, or recycle here.
			InnerStandingShape =  SettingsForInnerShape.Create().Get();
			//TODO: set up mInnerBodyIDOverride once we get to full determinism. all other shapes are ordered using queuing.
			mCharacterSettings.mInnerBodyShape = InnerStandingShape;
			mCharacter = new CharacterVirtual(&mCharacterSettings, mInitialPosition, Quat::sIdentity(), 0, World.Get());
			mCharacter->SetCharacterVsCharacterCollision(CVCColliderSystem); // see https://github.com/jrouwe/JoltPhysics/blob/e3ed3b1d33f3a0e7195fbac8b45b30f0a5c8a55b/UnitTests/Physics/CharacterVirtualTests.cpp#L759
			mEffectiveVelocity = Vec3::sZero();
			ret = mCharacter->GetInnerBodyID(); //I am going to regret this somehow. Update: I did.
			mCharacter->SetListener(mListener.Get());
			mUpdateSettings.mWalkStairsStepUp = {0.1, 0.6, 0.1};
		}
	}
	return ret;
}

void FBCharacter::StepCharacter()
{
	Vec3 MyVelo = mCharacter->GetLinearVelocity();
	Vec3 current_vertical = Vec3(0, MyVelo.GetY(), 0);
	Vec3 ground_velocity = mCharacter->GetGroundVelocity();
	bool bOnGround = (mCharacter->GetGroundState() == CharacterVirtual::EGroundState::OnGround);

	// VERTICAL: carry in air (gravity + jump), zero on ground
	Vec3 carry_vertical = bOnGround ? Vec3::sZero() : current_vertical * mThrottleModel.GetX();

	// HORIZONTAL: pre-smoothed by PrepareCharacterStep (already acceleration-applied)
	Vec3 smoothed_horizontal = Vec3(mLocomotionUpdate.GetX(), 0, mLocomotionUpdate.GetZ())
		* mThrottleModel.GetZ();
	mLocomotionUpdate = Vec3::sZero();

	// Moving platform
	if (bOnGround && !ground_velocity.IsNearZero())
		smoothed_horizontal += Vec3(ground_velocity.GetX(), 0, ground_velocity.GetZ());

	// Gravity
	Vec3 gravity_v = Vec3::sZero();
	if (World)
	{
		gravity_v = Vec3(0, mGravity.GetY() * mDeltaTime * mThrottleModel.GetY(), 0);
	}

	// Forces (jump impulse via OtherForce)
	Vec3 forces = mForcesUpdate * mThrottleModel.GetW();
	mForcesUpdate = Vec3::sZero();

	Vec3 new_velocity = smoothed_horizontal + carry_vertical + gravity_v + forces;
	
	RVec3 start_pos = GetPosition();
	//allow single frame exccession
	//TODO: factor this into a stupid constant.
	float Speed = new_velocity.Length();
	if (Speed > 1.0e-6f)
	{
		float SpeedLimit = min(Speed, (mMaxSpeed * 1.20f));
		Vec3 clamped = (new_velocity / Speed) * SpeedLimit;
		mCharacter->SetLinearVelocity(clamped);
	}
	else
	{
		mCharacter->SetLinearVelocity(Vec3::sZero());
	}
	// Update the character position. splitting this into two half-length updates allows you to get VERY
	// fine grained control by moving them around with respect to the clamp and update.
    {
    	TempAllocatorMalloc allocator;
    	mCharacter->ExtendedUpdate(mDeltaTime,
    							   mGravity,
    							   mUpdateSettings,
    							   World->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
    							   World->GetDefaultLayerFilter(Layers::MOVING),
    							   IgnoreSingleBodyFilter(mCharacter->GetInnerBodyID()),
    							   {},
    							   allocator);
    }

	// Update character velocity for carry over.
	Speed = new_velocity.Length();
	if (Speed > 1.0e-6f)
	{
		float SpeedLimit = min(Speed, (mMaxSpeed));
		Vec3 clamped = (new_velocity / Speed) * SpeedLimit;
		mCharacter->SetLinearVelocity(clamped);
	}
	else
	{
		mCharacter->SetLinearVelocity(Vec3::sZero());
	}
	mEffectiveVelocity = (mDeltaTime > 1.0e-6f)
		? Vec3(GetPosition() - start_pos) / mDeltaTime
		: Vec3::sZero();
}

void FBCharacter::IngestUpdate(FBPhysicsInput& input)
{
	switch (input.Action)
	{
	case PhysicsInputType::Rotation:
		mCapsuleRotationUpdate = input.State;
		break;
	case PhysicsInputType::OtherForce:
		// TODO: IDK this is a kludge for now since we removed the 100.0 divide by in CoordinateUtils::ToBarrageForce
		mForcesUpdate += input.State.GetXYZ() / 100.0;
		break;
	case PhysicsInputType::SelfMovement:
		mLocomotionUpdate += input.State.GetXYZ()/ 100.0;
		break;
	case PhysicsInputType::Throttle:
		//Throttle controls the four key forces acting on a character by scaling them.
		mThrottleModel = input.State;
		break;
	case PhysicsInputType::SetCharacterGravity:
		mGravity = input.State.GetXYZ()/ 100.0;
		break;
	case PhysicsInputType::SetPosition:
		SetPosition(input.State.GetXYZ());	
		break;
	case PhysicsInputType::ResetForces:
		mLocomotionUpdate = JPH::Vec3::sZero();
		mForcesUpdate = JPH::Vec3::sZero();
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("FBCharacter::IngestUpdate: Received unimplemented input.Action = [%d]"), input.Action);
	}
}

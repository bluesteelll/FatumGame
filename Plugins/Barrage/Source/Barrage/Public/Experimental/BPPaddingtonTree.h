// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once
#include "IsolatedJoltIncludes.h"
#include "FlattenedBodyBox.h"
#include "PaddingtonTree.h"

PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
JPH_NAMESPACE_BEGIN
	/// Fast SIMD based quad tree BroadPhase that is multithreading aware and tries to do a minimal amount of locking.
	/// This variation uses a precomputed adaptive top node, triple buffers, and prefetches.
	/// It's called a Paddington Tree because that's my dog's name.
	/// It's not actually a cool backronym. Sorry. I just love my dog.
	// ReSharper disable once CppClassNeedsConstructorBecauseOfUninitializedMember
	// adds layer-count monomorphism, allowing signficantly more elegant tree handling.
	template <uint32_t mNumLayers>
	class JPH_EXPORT BPPaddingtonTree final : public BroadPhase
	{
	public:
		JPH_OVERRIDE_NEW_DELETE


		using NodeBlob = std::vector<BodyBoxFlatCopy>;
		/// Destructor

	private:
		/// Helper struct for AddBodies handle
		struct LayerState
		{
			JPH_OVERRIDE_NEW_DELETE

			BodyID* mBodyStart = nullptr;
			BodyID* mBodyEnd;
			PaddingtonTree::AddState mAddState;
			std::shared_ptr<NodeBlob> mBodies;
			//switch to pool once that's in place and use something like = NodeBlob(1024)); with ya boy the blind alloc
		};

		using Tracking = PaddingtonTree::Tracking;
		using TrackingVector = PaddingtonTree::TrackingVector;

		/// Max amount of bodies we support
		size_t mMaxBodies = 0;

		/// Array that for each BodyID keeps track of where it is located in which tree
		TrackingVector mTracking;

		/// Node allocator for all trees
		/// Paddington trees take care of their own node pooling for reasons that will become obvious.
		//PaddingtonTree::Allocator mAllocator;

		/// Information about broad phase layers
		const BroadPhaseLayerInterface* mBroadPhaseLayerInterface = nullptr;

		/// One tree per object layer. Update swaps. Rather than locking queries for update or framesync, we allow a query to hold
		/// a ref per the holdopen pattern used throughout barrage and artillery. this is _significantly_ faster.
		std::shared_ptr<PaddingtonTree> mLayers[mNumLayers];

		/// UpdateState implementation for this tree used during UpdatePrepare/Finalize()
		struct UpdateStateImpl
		{
			PaddingtonTree* mTree;
			PaddingtonTree::UpdateState mUpdateState;
		};

		static_assert(sizeof(UpdateStateImpl) <= sizeof(UpdateState));
		static_assert(alignof(UpdateStateImpl) <= alignof(UpdateState));

		/// Mutex that prevents object modification during UpdatePrepare/Finalize()
		SharedMutex mUpdateMutex;

		/// We double buffer all trees so that we can query while building the next one and we destroy the old tree the next physics update.
		/// This structure ensures that we wait for queries that are still using the old tree.
		mutable SharedMutex mQueryLocks[2];

		/// This index indicates which lock is currently active, it alternates between 0 and 1
		atomic<uint32> mQueryLockIdx{0};

		/// This is the next tree to update in UpdatePrepare()
		uint32 mNextLayerToUpdate = 0;
		bool RequireFull = false;

public:
		// THE HEART OF THE CLASS, FOLKS
		// implement Broadphase Interface
		virtual ~BPPaddingtonTree() override
		{
		}

		virtual void Init(BodyManager* inBodyManager,
		                  const BroadPhaseLayerInterface& inLayerInterface) override
		{
			BroadPhase::Init(inBodyManager, inLayerInterface);

			// Store input parameters
			mBroadPhaseLayerInterface = &inLayerInterface;
			
			// Store max bodies
			mMaxBodies = inBodyManager->GetMaxBodies();

			// Initialize tracking data
			mTracking.resize(mMaxBodies);
			// We use double the amount of nodes while rebuilding the tree during Update()

			// Init sub trees
			mLayers = new PaddingtonTree [mNumLayers];
			for (uint l = 0; l < mNumLayers; ++l)
			{
				mLayers[l].Init(); //trees share an allocator, which is _very bad_ for memory locality.

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		// Set the name of the layer
		mLayers[l].SetName(inLayerInterface.GetBroadPhaseLayerName(BroadPhaseLayer(BroadPhaseLayer::Type(l))));
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED
			}
		}

		virtual void FrameSync() override
		{
			JPH_PROFILE_FUNCTION();
			
			UniqueLock root_lock(
				mQueryLocks[mQueryLockIdx ^ 1]
				JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseQuery));

			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
				mLayers[l].DiscardOldTree();
		}

		//this is paired with the artillery busy worker, which always optimizes once every 128 ticks, and never while a step is running.
		//you may need to revisit guarantees here around memory allocators or you'll probably get EFFED in the allocator. 
		virtual void BPPaddingtonTree::Optimize() override
		{
			JPH_PROFILE_FUNCTION();

			FrameSync();

			LockModifications();

			for (uint l = 0; l < mNumLayers; ++l)
			{
				PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies())
				{
					PaddingtonTree::UpdateState update_state;
					tree.UpdatePrepare(mBodyManager->GetBodies(), mTracking, update_state, true);
					tree.UpdateFinalize(mBodyManager->GetBodies(), mTracking, update_state);
				}
			}

			UnlockModifications();

			mNextLayerToUpdate = 0;
		}

		virtual void LockModifications() override
		{
			// From this point on we prevent modifications to the tree
			PhysicsLock::sLock(mUpdateMutex JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseUpdate));
		}

		virtual BroadPhase::UpdateState UpdatePrepare() override
		{
			// LockModifications should have been called
			JPH_ASSERT(mUpdateMutex.is_locked());

			// Create update state
			UpdateState update_state;
			UpdateStateImpl* update_state_impl = reinterpret_cast<UpdateStateImpl*>(&update_state);

			// Loop until we've seen all layers
			for (uint iteration = 0; iteration < mNumLayers; ++iteration)
			{
				// Get the layer
				PaddingtonTree& tree = mLayers[mNextLayerToUpdate];
				mNextLayerToUpdate = (mNextLayerToUpdate + 1) % mNumLayers;

				// If it is dirty we update this one
				if (tree.HasBodies() && tree.IsDirty() && tree.CanBeUpdated())
				{
					update_state_impl->mTree = &tree;
					bool capture = RequireFull; //this is atomic but offers no order guarantees.
					RequireFull = false; //we set this to false NOW but still execute the full update.
					//this reduces the chance of a missed remove req to near zero.
					//TODO: switch to atomic for sanity?
					tree.UpdatePrepare(mBodyManager->GetBodies(), mTracking, update_state_impl->mUpdateState, capture);

					return update_state;
				}
			}

			// Nothing to update
			update_state_impl->mTree = nullptr;
			return update_state;
		}

		virtual void UpdateFinalize(const UpdateState& inUpdateState) override
		{
			// LockModifications should have been called
			JPH_ASSERT(mUpdateMutex.is_locked());

			// Test if a tree was updated
			const UpdateStateImpl* update_state_impl = reinterpret_cast<const UpdateStateImpl*>(&inUpdateState);
			if (update_state_impl->mTree == nullptr)
				return;

			update_state_impl->mTree->UpdateFinalize(mBodyManager->GetBodies(), mTracking,
			                                         update_state_impl->mUpdateState);

			// Make all queries from now on use the new lock
			mQueryLockIdx = mQueryLockIdx ^ 1;
		}

		virtual void UnlockModifications() override
		{
			// From this point on we allow modifications to the tree again
			PhysicsLock::sUnlock(
				mUpdateMutex JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseUpdate));
		}

		BroadPhase::AddState BPPaddingtonTree::AddBodiesPrepare(BodyID* ioBodies, int inNumber)
		{
			JPH_PROFILE_FUNCTION();

			if (inNumber <= 0)
				return nullptr;

			const BodyVector& bodies = mBodyManager->GetBodies();
			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			LayerState* state = new LayerState [mNumLayers];
			for (uint32_t ii = 0; ii < mNumLayers; ++ii)
			{
				state[ii].mBodies = std::make_shared<NodeBlob>(256);
			}
			// bin bodies by layer
			Body* const * const bodies_ptr = bodies.data(); // C pointer or else sort is incredibly slow in debug mode
			BodyID *b_start = ioBodies, *b_end = ioBodies + inNumber;
			//single pass instead of quicksort. skipping around in bodies is deadly to our perf. qs is broadly faster, but causes more cache disorder
			//this also means we walk out with a cached version of the body data we need, and we'll only touch body manager one more time.
			for (int i = 0; i < inNumber; ++i)
			{
				auto& body = bodies_ptr[ioBodies[i].GetIndexAndSequenceNumber()];
				auto BBBWB = body->GetWorldSpaceBounds(); //boundingbox body:worldspace bounds. okay, okay, sorry.
				auto Cent = BBBWB.GetCenter();
				auto Extt = BBBWB.GetExtent();
				auto shadow = BodyBoxFlatCopy(Cent.GetX(), Cent.GetY(), Cent.GetZ(),
				                              Extt.GetX() + 1, Extt.GetY() + 1, Extt.GetZ() + 1,
				                              body->GetObjectLayer(),
				                              body->IsKinematic(),
				                              body->IsDynamic(),
				                              body->IsSensor(),
				                              body->IsRigidBody(),
				                              body->IsStatic(),
				                              body->IsActive(),
				                              body->GetCollideKinematicVsNonDynamic(),
				                              ioBodies[i]);
				state[shadow.layer].mBodies->push_back(shadow);
			}

			for (uint32_t shadow = 0; shadow < mNumLayers; ++shadow)
			{
				// Get broadphase layer
				if (state[shadow].mBodies->size() > 0)
				{
					LayerState& layer_state = state[shadow];
					// Keep track of state for this layer

					// Insert all bodies of the same layer
					//mLayers[shadow].AddBodiesPrepare(bodies, mTracking, b_start, int(b_mid - b_start), layer_state.mAddState);

					// Keep track in which tree we placed the object
					for (auto& b : *(state[shadow].mBodies))
					{
						uint32 index = b.meta.GetIndex();
						Tracking& t = mTracking[index];
						t.mBroadPhaseLayer = shadow;
						t.mObjectLayer = bodies[index]->GetObjectLayer();
					}
				}
			}

			return state;
		}

		void BPPaddingtonTree::AddBodiesFinalize(BodyID* ioBodies, int inNumber, AddState inAddState)
		{
			JPH_PROFILE_FUNCTION();

			if (inNumber <= 0)
			{
				JPH_ASSERT(inAddState == nullptr);
				return;
			}

			// This cannot run concurrently with UpdatePrepare()/UpdateFinalize()
			//todo: switch this to a try lock with a thread safe fallback. that will allow us to isolate our memory allocations
			//which means we can switch to a brute-stupid memory allocation solution like the threadblind alloc for extreme speed.
			SharedLock lock(mUpdateMutex JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseUpdate));

			BodyVector& bodies = mBodyManager->GetBodies();
			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			LayerState* state = (LayerState*)inAddState;

			for (BroadPhaseLayer::Type broadphase_layer = 0; broadphase_layer < mNumLayers; broadphase_layer++)
			{
				const LayerState& l = state[broadphase_layer];
				if (l.mBodyStart != nullptr)
				{
					// Insert all bodies of the same layer
					mLayers[broadphase_layer].AddBodiesFinalize(mTracking, int(l.mBodyEnd - l.mBodyStart), l.mAddState);

					// Mark added to broadphase
					for (const BodyID* b = l.mBodyStart; b < l.mBodyEnd; ++b)
					{
						uint32 index = b->GetIndex();
						JPH_ASSERT(bodies[index]->GetID() == *b,
						           "Provided BodyID doesn't match BodyID in body manager");
						JPH_ASSERT(mTracking[index].mBroadPhaseLayer == broadphase_layer);
						JPH_ASSERT(mTracking[index].mObjectLayer == bodies[index]->GetObjectLayer());
						JPH_ASSERT(!bodies[index]->IsInBroadPhase());
						bodies[index]->SetInBroadPhaseInternal(true);
					}
				}
			}

			delete [] state;
		}

		void BPPaddingtonTree::AddBodiesAbort(BodyID* ioBodies, int inNumber, AddState inAddState)
		{
			throw; //absolutely not. just remove them like a normal human, for the love of god.
		}

		//TODO: just lie less?
		void BPPaddingtonTree::RemoveBodies(BodyID* ioBodies, int inNumber)
		{
			JPH_PROFILE_FUNCTION();

			RequireFull = true; // we DO NOT manually remove things. I lied above.
		}

		void BPPaddingtonTree::NotifyBodiesAABBChanged(BodyID* ioBodies, int inNumber, bool inTakeLock)
		{
			JPH_PROFILE_FUNCTION();

			if (inNumber <= 0)
				return;

			// This cannot run concurrently with UpdatePrepare()/UpdateFinalize()
			if (inTakeLock)
				PhysicsLock::sLockShared(
					mUpdateMutex JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseUpdate));
			else
				JPH_ASSERT(mUpdateMutex.is_locked());

			const BodyVector& bodies = mBodyManager->GetBodies();
			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Sort bodies on layer
			const Tracking* tracking = mTracking.data(); // C pointer or else sort is incredibly slow in debug mode
			//QuickSort(ioBodies, ioBodies + inNumber, [tracking](BodyID inLHS, BodyID inRHS) { return tracking[inLHS.GetIndex()].mBroadPhaseLayer < tracking[inRHS.GetIndex()].mBroadPhaseLayer; });

			BodyID *b_start = ioBodies, *b_end = ioBodies + inNumber;
			while (b_start < b_end)
			{
				// Get broadphase layer
				BroadPhaseLayer::Type broadphase_layer = tracking[b_start->GetIndex()].mBroadPhaseLayer;
				JPH_ASSERT(broadphase_layer != (BroadPhaseLayer::Type)cBroadPhaseLayerInvalid);

				// Find first body with different layer
				BodyID* b_mid = std::upper_bound(b_start, b_end, broadphase_layer,
				                                 [tracking](BroadPhaseLayer::Type inLayer, BodyID inBodyID)
				                                 {
					                                 return inLayer < tracking[inBodyID.GetIndex()].mBroadPhaseLayer;
				                                 });

				// Notify all bodies of the same layer changed
				mLayers[broadphase_layer].NotifyBodiesAABBChanged(bodies, mTracking, b_start, int(b_mid - b_start));

				// Repeat
				b_start = b_mid;
			}

			if (inTakeLock)
				PhysicsLock::sUnlockShared(
					mUpdateMutex JPH_IF_ENABLE_ASSERTS(, mLockContext, EPhysicsLockTypes::BroadPhaseUpdate));
		}

		void BPPaddingtonTree::NotifyBodiesLayerChanged(BodyID* ioBodies, int inNumber)
		{
			//this is no longer supported.
			throw;
		}

		void BPPaddingtonTree::CastRay(const RayCast& inRay, RayCastBodyCollector& ioCollector,
		                               const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                               const ObjectLayerFilter& inObjectLayerFilter) const
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CastRay(inRay, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		void BPPaddingtonTree::CollideAABox(const AABox& inBox, CollideShapeBodyCollector& ioCollector,
		                                    const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                                    const ObjectLayerFilter& inObjectLayerFilter) const
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CollideAABox(inBox, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		void BPPaddingtonTree::CollideSphere(Vec3Arg inCenter, float inRadius, CollideShapeBodyCollector& ioCollector,
		                                     const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                                     const ObjectLayerFilter& inObjectLayerFilter) const
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CollideSphere(inCenter, inRadius, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		virtual void CollidePoint(Vec3Arg inPoint, CollideShapeBodyCollector& ioCollector,
		                          const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                          const ObjectLayerFilter& inObjectLayerFilter) const override
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CollidePoint(inPoint, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		virtual void CollideOrientedBox(const OrientedBox& inBox, CollideShapeBodyCollector& ioCollector,
		                                const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                                const ObjectLayerFilter& inObjectLayerFilter) const override
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CollideOrientedBox(inBox, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		virtual void CastAABoxNoLock(const AABoxCast& inBox, CastShapeBodyCollector& ioCollector,
		                             const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                             const ObjectLayerFilter& inObjectLayerFilter) const override
		{
			JPH_PROFILE_FUNCTION();

			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Loop over all layers and test the ones that could hit
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
			{
				const PaddingtonTree& tree = mLayers[l];
				if (tree.HasBodies() && inBroadPhaseLayerFilter.ShouldCollide(BroadPhaseLayer(l)))
				{
					JPH_PROFILE(tree.GetName());
					tree.CastAABox(inBox, ioCollector, inObjectLayerFilter, mTracking);
					if (ioCollector.ShouldEarlyOut())
						break;
				}
			}
		}

		virtual void CastAABox(const AABoxCast& inBox, CastShapeBodyCollector& ioCollector,
		                       const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                       const ObjectLayerFilter& inObjectLayerFilter) const override
		{
			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			CastAABoxNoLock(inBox, ioCollector, inBroadPhaseLayerFilter, inObjectLayerFilter);
		}

		virtual void FindCollidingPairs(BodyID* ioActiveBodies, int inNumActiveBodies,
		                                float inSpeculativeContactDistance,
		                                const ObjectVsBroadPhaseLayerFilter& inObjectVsBroadPhaseLayerFilter,
		                                const ObjectLayerPairFilter& inObjectLayerPairFilter,
		                                BodyPairCollector& ioPairCollector) const override
		{
			JPH_PROFILE_FUNCTION();

			const BodyVector& bodies = mBodyManager->GetBodies();
			JPH_ASSERT(mMaxBodies == mBodyManager->GetMaxBodies());

			// Note that we don't take any locks at this point. We know that the tree is not going to be swapped or deleted while finding collision pairs due to the way the jobs are scheduled in the PhysicsSystem::Update.

			// Sort bodies on layer
			const Tracking* tracking = mTracking.data(); // C pointer or else sort is incredibly slow in debug mode
			//QuickSort(ioActiveBodies, ioActiveBodies + inNumActiveBodies, [tracking](BodyID inLHS, BodyID inRHS) { return tracking[inLHS.GetIndex()].mObjectLayer < tracking[inRHS.GetIndex()].mObjectLayer; });

			BodyID *b_start = ioActiveBodies, *b_end = ioActiveBodies + inNumActiveBodies;
			while (b_start < b_end)
			{
				// Get broadphase layer
				ObjectLayer object_layer = tracking[b_start->GetIndex()].mObjectLayer;
				JPH_ASSERT(object_layer != cObjectLayerInvalid);

				// Find first body with different layer
				BodyID* b_mid = std::upper_bound(b_start, b_end, object_layer,
				                                 [tracking](ObjectLayer inLayer, BodyID inBodyID)
				                                 {
					                                 return inLayer < tracking[inBodyID.GetIndex()].mObjectLayer;
				                                 });

				// Loop over all layers and test the ones that could hit
				for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
				{
					const PaddingtonTree& tree = mLayers[l];
					if (tree.HasBodies() && inObjectVsBroadPhaseLayerFilter.ShouldCollide(
						object_layer, BroadPhaseLayer(l)))
					{
						JPH_PROFILE(tree.GetName());
						tree.FindCollidingPairs(bodies, b_start, int(b_mid - b_start), inSpeculativeContactDistance,
						                        ioPairCollector, inObjectLayerPairFilter);
					}
				}

				// Repeat
				b_start = b_mid;
			}
		}

		virtual AABox GetBounds() const override
		{
			// Prevent this from running in parallel with node deletion in FrameSync(), see notes there
			shared_lock lock(mQueryLocks[mQueryLockIdx]);

			AABox bounds;
			for (BroadPhaseLayer::Type l = 0; l < mNumLayers; ++l)
				bounds.Encapsulate(mLayers[l].GetBounds());
			return bounds;
		}
	};
template class BPPaddingtonTree<JOLT::BroadPhaseLayers::NUM_LAYERS>;

typedef BPPaddingtonTree<JOLT::BroadPhaseLayers::NUM_LAYERS> MMBP_PaddingtonTree;
JPH_NAMESPACE_END

PRAGMA_POP_PLATFORM_DEFAULT_PACKING

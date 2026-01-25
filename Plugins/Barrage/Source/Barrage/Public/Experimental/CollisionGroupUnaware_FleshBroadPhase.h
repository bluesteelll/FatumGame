#pragma once
#include "FlattenedBodyBox.h"
#include "IsolatedJoltIncludes.h"

PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
#include "Embedder.h"
#include "Grouping/sketch/bbmh.h"

JPH_NAMESPACE_BEGIN

//we use a novel LSH solution with a shadow-copying approach to create
// an exceptionally fast, if memory HUNGRY, attack on the broadphase problem.
// seriously, this thing will devour your ram.
//The name is serious and exact, sadly. The algorithm we use is called FLESH - FLINNG + Line Embedding Semi-Hash
//It is directly related to a number of different existing algorithms, include flinng, bbminhash, probabilistic counting,
//and a lostech approach for embedding lines as high dimensional points. It is deterministic, in that the same inputs produce
//the same outputs, and uses very few floating point operations overall.
//Right now, it remains highly experimental but it's closing in on usability fast. There are a couple moderately central problems to solve.
//But at the moment, we actually just beat them to death with by using very high values for topk results, AND filtering for collision earlier
//than usual. We're able to do this by taking shadow copies of just about everything.
//
//Currently, we don't support collision groups. This is pretty bad, I'll be straight with you, but we also don't use them yet. Adding them would be
//as simple as either adding them to the shadowing struct we use for bodies, but it'd be better to keep a lookup table of all the groups as refs
//and then add a flyweight link into that index to the shadow struct. With some care, this is actually MORE likely to stay cache hot, as the collision group
//data structure is 16 bytes and currently, the entire shadow is only 24. going with the flyweight makes it 32b, so at least two are fetched during a cache prefetch on
//most machines. Fetching two means that you almost never get a cache miss when doing forward iteration, and this is a huge performance improvement.
//
//It may be possible to condense the collision groups significantly. if so, then you're HooOooOooome free. good luck with that, future Jake.
//--JMK
	class CollisionGroupUnaware_FleshBroadPhase : public JPH::BroadPhase
	{
		static inline constexpr uint32_t DefaultMaxAllowedHitsPerCast = 2048;
		static inline constexpr float AllowedAddedScanRangeMult = 3;
		static inline constexpr uint32_t PointsPerBB = 2;
		static inline constexpr float SpeculativeContactDistance = 0.1;
		static constexpr uint32_t TOPK = 20;
#define ___LOCO_RCFF(x) (x)
		//min
#define __LOCO_FFABV(m)		 ___LOCO_RCFF(m.x), ___LOCO_RCFF(m.y), ___LOCO_RCFF(m.z)
		//max
#define __LOCO_FFABVM(m)	 ___LOCO_RCFF( (m.x + m.xBound) ), ___LOCO_RCFF((m.y + m.yBound)), ___LOCO_RCFF(m.z + m.zBound)
		//center
#define __LOCO_FFABVC(m)	 ___LOCO_RCFF( (m.x + (m.xBound/2)) ), ___LOCO_RCFF((m.y + (m.yBound/2))), ___LOCO_RCFF((m.z + (m.zBound/2)))
#define __LOCO_FFABVX(m)	 ___LOCO_RCFF( (m.x + m.xBound) ), ___LOCO_RCFF((m.y)), ___LOCO_RCFF(m.z)
#define __LOCO_FFABVXY(m)	 ___LOCO_RCFF( (m.x + m.xBound) ), ___LOCO_RCFF((m.y + m.yBound)), ___LOCO_RCFF(m.z)
#define __LOCO_FFABVXZ(m)	 ___LOCO_RCFF( (m.x + m.xBound) ), ___LOCO_RCFF((m.y)), ___LOCO_RCFF(m.z + m.zBound)
#define __LOCO_FFABVY(m)	 ___LOCO_RCFF( (m.x) ), ___LOCO_RCFF((m.y + m.yBound)), ___LOCO_RCFF(m.z)
#define __LOCO_FFABVYZ(m)	 ___LOCO_RCFF( (m.x) ), ___LOCO_RCFF((m.y + m.yBound)), ___LOCO_RCFF((m.z + m.zBound) )
#define __LOCO_FFABVZ(m)	 ___LOCO_RCFF( (m.x ) ), ___LOCO_RCFF((m.y )), ___LOCO_RCFF(m.z + m.zBound)
		////////////////////////////////////////////////
		///BodyBoxSafeShadow
		////////////////////////////////////////////////
		//This struct is an intentional copy of certain attributes of a body sufficient to fully describe it for most steps of
		//the broadphase as well as provide a little meta info if we need it. We regenerate these copies during build, so we don't need to be
		//aware of notify in either direction. This allows us to avoid any locking and push our byte width down below a cache line fill

		//we should move layer out, as seen below, but it actually buys us nothing in most packing schemas. You could either use the below schema
		//or you could do something a bit diff where you always allocate "blobs" of these fellas such that they're pack-aligned.

		/* example 64b aligned would look something like this for our purposes.
		* 
		* struct A_BS
		{
		uint32_t meta = 0;
		//these would likely need to be unrolled into their component variables to actually avoid getting wrecked by packing padding.
		//And yes, I would waste memory before I used pack(0), because pack(0) has a lot of other costs. Profile pack0 struct access sometime.
		BodyBoxSafeShadow A;
		BodyBoxSafeShadow B;
		BodyBoxSafeShadow C;
		};
		*/
	public:
		using BodyBoxSafeShadow = BodyBoxFlatCopy;


		JPH_OVERRIDE_NEW_DELETE


		double MaxExtent = 0;
		void TestInvokeBuild();
		using EMBED = Embedder<2>;
		using FLESH = EMBED::FLESH;
		using NodeBlob = std::vector<BodyBoxSafeShadow>;
		/// Handle used during adding bodies to the broadphase
		using AddState = void*;
		virtual void CastRay(const JPH::RayCast& inRay, JPH::RayCastBodyCollector& ioCollector,
		                     const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                     const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		void CastRay(const RayCast& inRay, RayCastBodyCollector& ioCollector,
		             const BroadPhaseLayerFilter& inBroadPhaseLayerFilter, const ObjectLayerFilter& inObjectLayerFilter,
		             uint32_t AllowedHits) const;
		bool ScoopUpHitsRay(RayCastBodyCollector& ioCollector, const ObjectLayerFilter& inObjectLayerFilter,
		                    uint32_t AllowedHits,
		                    Vec3 origin, RayInvDirection inv_direction, std::vector<uint32_t> tset,
		                    float early_out_fraction,
		                    uint32_t hits) const;
		void ApproxCastRay(const RayCast& inRay, RayCastBodyCollector& ioCollector,
		                   const BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                   const ObjectLayerFilter& inObjectLayerFilter,
		                   uint32_t AllowedHits) const;
		virtual void CollideAABox(const JPH::AABox& inBox, JPH::CollideShapeBodyCollector& ioCollector,
		                          const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                          const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		virtual void CollideSphere(JPH::Vec3Arg inCenter, float inRadius, JPH::CollideShapeBodyCollector& ioCollector,
		                           const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                           const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		virtual void CollidePoint(JPH::Vec3Arg inPoint, JPH::CollideShapeBodyCollector& ioCollector,
		                          const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                          const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		virtual void CollideOrientedBox(const JPH::OrientedBox& inBox, JPH::CollideShapeBodyCollector& ioCollector,
		                                const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                                const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		template <class behavior, class T>
		bool Accumulate(const T& inBox, behavior collector, const ObjectLayerFilter& inObjectLayerFilter, float lenny,
		                UnorderedSet<uint32_t>& Setti, std::vector<uint32_t>& result) const;
		virtual void CastAABox(const JPH::AABoxCast& inBox, JPH::CastShapeBodyCollector& ioCollector,
		                       const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter = {},
		                       const JPH::ObjectLayerFilter& inObjectLayerFilter = {}) const override;
		virtual void Init(JPH::BodyManager* inBodyManager,
		                  const JPH::BroadPhaseLayerInterface& inLayerInterface) override;

		//legitimately, most functions do nothing with the hungry broadphase.
		virtual void Optimize() override;
		virtual void FrameSync() override;
		virtual void LockModifications() override;
		virtual UpdateState UpdatePrepare() override;
		virtual void UpdateFinalize(const UpdateState& inUpdateState) override;
		void RebuildLSH();
		virtual void UnlockModifications() override;
		virtual AddState AddBodiesPrepare(JPH::BodyID* ioBodies, int inNumber) override;
		virtual void AddBodiesFinalize(JPH::BodyID* ioBodies, int inNumber, AddState inAddState) override;

		//abort is odd. we don't support it, but we also have nothing to abort.
		virtual void AddBodiesAbort(JPH::BodyID* ioBodies, int inNumber, AddState inAddState) override;

		virtual void RemoveBodies(JPH::BodyID* ioBodies, int inNumber) override;
		virtual void NotifyBodiesAABBChanged(JPH::BodyID* ioBodies, int inNumber, bool inTakeLock = true) override;
		virtual void NotifyBodiesLayerChanged(JPH::BodyID* ioBodies, int inNumber) override;
		virtual void FindCollidingPairs(JPH::BodyID* ioActiveBodies, int inNumActiveBodies,
		                                float inSpeculativeContactDistance,
		                                const JPH::ObjectVsBroadPhaseLayerFilter& inObjectVsBroadPhaseLayerFilter,
		                                const JPH::ObjectLayerPairFilter& inObjectLayerPairFilter,
		                                JPH::BodyPairCollector& ioPairCollector) const override;

		virtual void CastAABoxNoLock(const JPH::AABoxCast& inBox, JPH::CastShapeBodyCollector& ioCollector,
		                             const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter,
		                             const JPH::ObjectLayerFilter& inObjectLayerFilter) const override;
		virtual JPH::AABox GetBounds() const override;
		void GenerateBBHashes(float oldM, FLESHPoint* Batch,
		                      std::vector<CollisionGroupUnaware_FleshBroadPhase::BodyBoxSafeShadow>::value_type& body);
		virtual ~CollisionGroupUnaware_FleshBroadPhase() override;

	private:
		//may need to macro this out for full platform support...
		//oh c++, never change*
		//no, seriously, please don't break ABI compatibility.
		//*as of C++20. even 11 is barely a usable language.
		FLESH::Arena ArenaFront = nullptr;
		FLESH::Arena ArenaBack = nullptr;
		std::shared_ptr<FLESH> LSH = std::make_shared<FLESH>((1024), ArenaFront);
		std::shared_ptr<FLESH> LSHShadow = std::make_shared<FLESH>((1024), ArenaBack);
		//we'll need to come back and roll these to a proper blob alloc.
		//and probably swap to LockFreeHashMap or allocation optimized hashmap. but for now, let's just get this thing WORKING
		std::shared_ptr<NodeBlob> mBodies = std::make_shared<NodeBlob>(NodeBlob(1024));
		std::shared_ptr<NodeBlob> mBodiesShadow = std::make_shared<NodeBlob>(NodeBlob(1024));
	};

	PRAGMA_POP_PLATFORM_DEFAULT_PACKING
JPH_NAMESPACE_END

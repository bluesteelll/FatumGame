// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once
#include "IsolatedJoltIncludes.h"
#include "PaddingtonDetail.h"
#include <Jolt/Core/NonCopyable.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhase.h>

//#define JPH_DUMP_BROADPHASE_TREE

PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
JPH_NAMESPACE_BEGIN
#define HOW_MANY_OBJECTS_DO_YOU_REALLY_NEED_GUYS 600000
	///
	/// remember when I said this wasn't an acronym?
	/// It's not, but it is actually a pretty useful mnemonic. these are basically very esoteric rtrees, in a sense.
	/// This is a parallel add-only tree built out of padded grid-tries of nvoxels.
	///
	/// Parallel Add Only
	/// The paddington tree only supports add, but supports add in smooth parallel using a swap-swap approach and reserve bit tricks.
	/// As a result, removes trigger a rebuild. because we tick so fast, removes are actually rare relative to the number of steps, though they tend
	/// to come in bursts as waves of bullets hit or objects unload in groups. As a result, a rebuild-on-remove policy made sense.
	/// allowing remove was basically going to add 3 months, and I have a game to make. Honestly, I'm only making this because I'm sad about my dog.
	/// 
	/// Grid Trie
	/// Each node of a paddington tree is a 256^K grid of voxels dividing a cube of space. An object or subnode is an AABB over this grid,
	/// effectively a span of voxels in K dimensions. This implementation only supports 3d. This thing is complete overkill for two d, and
	/// I use LSH stuff for higher D. You can find that over in locomo, and a prototype broadphase we haven't gotten working yet can be found
	/// in the FLESH BroadPhase. No, I don't think it was cursed. Why do you ask?
	///
	/// Padded
	/// Oh boy. Here we go. So we save 8 bytes by making the grid a cube, right? but we allow our aabb ranges to be non-cube.
	/// There's a version that doesn't, but it doesn't give very good geometric fit. A given node's AABB is a rectangular prism
	/// but the internal space is a cube. This is the padding. I wouldn't have done this, but the difference between 72 bytes and 64
	/// is about a 20% perf improvement on some platforms for some workloads. However, the padding has some interesting effects.
	/// Namely, it means that we can use double entry, if we need to, to avoid certain search problems. we currently don't, but
	/// I'm thinking about it, and may tackle it after Lions ships.
	///
	/// These grids vary in resolution for specifying bodies but each layer has a float's worth of precision once the space is open.
	///
	/// Why?
	/// An entire shape is 6 bytes for rectangular prisms, and a miniscule 4 bytes for spheres. That doesn't sound like a big deal,
	/// but it is, mainly because each individual axis is incredibly small for sets of shapes. This allows us to go very very fast.
	/// We also use double precision for our root node, allowing clusters of nodes to be extremely far apart while also supporting
	/// a very aggressive 256 nodes in a single elegant static allocated structure.
	///
	/// What if I have less than 256 objects?
	/// If you have less than 256 objects, please use either bruteforce or the existing jolt quadtree. both are incredible options.
	/// Paddington Trees are really best suited for coarser broadphases that have very large numbers of objects.
	///
	/// A note on paddington tree allocation behavior: paddington trees always consume at least 4 kilos of memory, broadly.
	/// Until the root node and the integrated first row of children are exhausted completely, the tree in question will not request an allocator.
	/// once it does, it will ask the BPPaddington Broadphase for an existing pool, or create an 8 meg pool. We use TLSF under the hood,
	/// which is completely unaware of threads.
	///
	///
	/// A NOTE ON BUFFERING
	/// The jolt quadtree manages its own buffering! this is REALLY slick, and is a great reason to prefer it for most
	/// workloads at the moment. We use a tick-linked hold-open pattern for query and lifecycle management instead.
	/// The backing memory is pooled, and we DO NOT call destructors for anything allocated into it. You have been warned.
	/// We may switch to a fixed allocator, but being able to use stl

	class JPH_EXPORT PaddingtonTree : public NonCopyable
	{
	public:
		JPH_OVERRIDE_NEW_DELETE

	private:
		// Forward declare
		using AtomicNodeID = PDTH::AtomicNodeID; //we can drop these.
		using Node = PDTH::Node;
		using RootNode = PDTH::RootNode;
		using Allocator = PDTH::Allocator;

		uint32 num_leaves = (uint32)(HOW_MANY_OBJECTS_DO_YOU_REALLY_NEED_GUYS + 1) / 2; // Assume 50% fill
		uint32 num_leaves_plus_internal_nodes = num_leaves + (num_leaves + 2) / 3;

		/// Class that points to either a body or a node in the tree
		class NodeID
		{
		public:
			JPH_OVERRIDE_NEW_DELETE

			/// Default constructor does not initialize
			inline NodeID() = default;

			/// Construct a node ID
			static inline NodeID sInvalid() { return NodeID(cInvalidNodeIndex); }

			static inline NodeID sFromBodyID(BodyID inID)
			{
				NodeID node_id(inID.GetIndexAndSequenceNumber());
				JPH_ASSERT(node_id.IsBody());
				return node_id;
			}

			static inline NodeID sFromNodeIndex(uint32 inIdx)
			{
				JPH_ASSERT((inIdx & cIsNode) == 0);
				return NodeID(inIdx | cIsNode);
			}

			/// Check what type of ID it is
			inline bool IsValid() const { return mID != cInvalidNodeIndex; }
			inline bool IsBody() const { return (mID & cIsNode) == 0; }
			inline bool IsNode() const { return (mID & cIsNode) != 0; }

			/// Get body or node index
			inline BodyID GetBodyID() const
			{
				JPH_ASSERT(IsBody());
				return BodyID(mID);
			}

			inline uint32 GetNodeIndex() const
			{
				JPH_ASSERT(IsNode());
				return mID & ~cIsNode;
			}

			/// Comparison
			inline bool operator ==(const BodyID& inRHS) const { return mID == inRHS.GetIndexAndSequenceNumber(); }
			inline bool operator ==(const NodeID& inRHS) const { return mID == inRHS.mID; }

		private:
			friend class AtomicNodeID;

			inline explicit NodeID(uint32 inID) : mID(inID)
			{
			}

			static const uint32 cIsNode = BodyID::cBroadPhaseBit;
			///< If this bit is set it means that the ID refers to a node, otherwise it refers to a body

			uint32 mID;
		};

		static_assert(sizeof(NodeID) == sizeof(BodyID), "Body id's should have the same size as NodeIDs");
	

		// Maximum size of the stack during tree walk
		static constexpr int cStackSize = 128;

		static_assert(sizeof(atomic<float>) == 4, "Assuming that an atomic doesn't add any additional storage");
		static_assert(sizeof(atomic<uint32>) == 4, "Assuming that an atomic doesn't add any additional storage");
		static_assert(std::is_trivially_destructible<Node>(), "Assuming that we don't have a destructor");

	public:
		//As trees click into and out of existence, once they saturate their root node, we will create a giant pool allocator for them.
		//Generally, we'll give each tree either 8 or 16 megs of memory, because memory is incredibly cheap now, and RAM's a big advantage
		//when you're picking CPU over GPU. Note that these are SEPARATE from the jolt temp allocator AND separate from each other.
		//And they are large. I want to be clear. I know that number looks scary. Tuning it downward is an exercise in pain.
		//As a result of the size of these memory slabs, the BPPaddingtonTree owns the process of pooling the pools.
		//Generally, it will go ahead and grab 8 upfront. Why 8? 64 megs of ram is the biggest number people don't flinch at.
		//I'd go bigger if I thought anyone would still use my library. Meh. --JMK
		

		RootNode Self;

		/// Data to track location of a Body in the tree
		struct Tracking
		{
			/// Constructor to satisfy the vector class
			Tracking() = default;

			Tracking(const Tracking& inRHS) : mBroadPhaseLayer(inRHS.mBroadPhaseLayer.load()),
			                                  mObjectLayer(inRHS.mObjectLayer.load()),
			                                  mBodyLocation(inRHS.mBodyLocation.load())
			{
			}

			/// Invalid body location identifier
			static const uint32 cInvalidBodyLocation = 0xffffffff;

			atomic<BroadPhaseLayer::Type> mBroadPhaseLayer = (BroadPhaseLayer::Type)cBroadPhaseLayerInvalid;
			atomic<ObjectLayer> mObjectLayer = cObjectLayerInvalid;
			atomic<uint32> mBodyLocation{cInvalidBodyLocation};
		};

		using TrackingVector = Array<Tracking>;

		/// Destructor
		~PaddingtonTree();

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	/// Name of the tree for debugging purposes
	void						SetName(const char *inName)			{ mName = inName; }
	inline const char *			GetName() const						{ return mName; }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

		/// Check if there is anything in the tree
		inline bool HasBodies() const { return mNumBodies != 0; }

		/// Check if the tree needs an UpdatePrepare/Finalize()
		inline bool IsDirty() const { return mIsDirty; }

		/// Initialization
		void Init(std::shared_ptr<PDTH::MemoryPoolPool> PoolAgent);

		struct UpdateState
		{
			NodeID mRootNodeID; ///< This will be the new root node id
		};

		/// Get the bounding box for this tree
		AABox GetBounds() const;

		/// Update the broadphase, needs to be called regularly to achieve a tight fit of the tree when bodies have been modified.
		/// UpdatePrepare() will build the tree, UpdateFinalize() will lock the root of the tree shortly and swap the trees and afterwards clean up temporary data structures.
		void UpdatePrepare(const BodyVector& inBodies, TrackingVector& ioTracking, UpdateState& outUpdateState,
		                   bool inFullRebuild);
		void UpdateFinalize(const BodyVector& inBodies, const TrackingVector& inTracking,
		                    const UpdateState& inUpdateState);

		/// Temporary data structure to pass information between AddBodiesPrepare and AddBodiesFinalize/Abort
		struct AddState
		{
			NodeID mLeafID = NodeID::sInvalid();
			AABox mLeafBounds;
		};

		/// Prepare adding inNumber bodies at ioBodyIDs to the quad tree, returns the state in outState that should be used in AddBodiesFinalize.
		/// This can be done on a background thread without influencing the broadphase.
		/// ioBodyIDs may be shuffled around by this function.
		void AddBodiesPrepare(const BodyVector& inBodies, TrackingVector& ioTracking, BodyID* ioBodyIDs, int inNumber,
		                      AddState& outState);

		/// Finalize adding bodies to the quadtree, supply the same number of bodies as in AddBodiesPrepare.
		void AddBodiesFinalize(TrackingVector& ioTracking, int inNumberBodies, const AddState& inState);

		/// Abort adding bodies to the quadtree, supply the same bodies and state as in AddBodiesPrepare.
		/// This can be done on a background thread without influencing the broadphase.
		void AddBodiesAbort(TrackingVector& ioTracking, const AddState& inState);

		/// Remove inNumber bodies in ioBodyIDs from the quadtree.
		void RemoveBodies(const BodyVector& inBodies, TrackingVector& ioTracking, const BodyID* ioBodyIDs,
		                  int inNumber);

		/// Call whenever the aabb of a body changes.
		void NotifyBodiesAABBChanged(const BodyVector& inBodies, const TrackingVector& inTracking,
		                             const BodyID* ioBodyIDs, int inNumber);

		/// Cast a ray and get the intersecting bodies in ioCollector.
		void CastRay(const RayCast& inRay, RayCastBodyCollector& ioCollector,
		             const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Get bodies intersecting with inBox in ioCollector
		void CollideAABox(const AABox& inBox, CollideShapeBodyCollector& ioCollector,
		                  const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Get bodies intersecting with a sphere in ioCollector
		void CollideSphere(Vec3Arg inCenter, float inRadius, CollideShapeBodyCollector& ioCollector,
		                   const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Get bodies intersecting with a point and any hits to ioCollector
		void CollidePoint(Vec3Arg inPoint, CollideShapeBodyCollector& ioCollector,
		                  const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Get bodies intersecting with an oriented box and any hits to ioCollector
		void CollideOrientedBox(const OrientedBox& inBox, CollideShapeBodyCollector& ioCollector,
		                        const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Cast a box and get intersecting bodies in ioCollector
		void CastAABox(const AABoxCast& inBox, CastShapeBodyCollector& ioCollector,
		               const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking) const;

		/// Find all colliding pairs between dynamic bodies, calls ioPairCollector for every pair found
		void FindCollidingPairs(const BodyVector& inBodies, const BodyID* inActiveBodies, int inNumActiveBodies,
		                        float inSpeculativeContactDistance, BodyPairCollector& ioPairCollector,
		                        const ObjectLayerPairFilter& inObjectLayerPairFilter) const;

#ifdef JPH_TRACK_BROADPHASE_STATS
	/// Sum up all the ticks spent in the various layers
	uint64						GetTicks100Pct() const;

	/// Trace the stats of this tree to the TTY
	void						ReportStats(uint64 inTicks100Pct) const;
#endif // JPH_TRACK_BROADPHASE_STATS

	private:
		/// Constants

		static const AABox cInvalidBounds; ///< Invalid bounding box using cLargeFloat
		static constexpr uint32 cInvalidNodeIndex = 0xffffffff; ///< Value used to indicate node index is invalid
		/// Caches location of body inBodyID in the tracker, body can be found in mNodes[inNodeIdx].mChildNodeID[inChildIdx]
		void GetBodyLocation(const TrackingVector& inTracking, BodyID inBodyID, uint32& outNodeIdx,
		                     uint32& outChildIdx) const;
		void SetBodyLocation(TrackingVector& ioTracking, BodyID inBodyID, uint32 inNodeIdx, uint32 inChildIdx) const;
		static void sInvalidateBodyLocation(TrackingVector& ioTracking, BodyID inBodyID);

		/// Get the current root of the tree
		JPH_INLINE const RootNode& GetCurrentRoot() const { return Self; }
		JPH_INLINE RootNode& GetCurrentRoot() { return Self; }

		/// Depending on if inNodeID is a body or tree node return the bounding box
		inline AABox GetNodeOrBodyBounds(const BodyVector& inBodies, NodeID inNodeID) const;

		/// Mark node and all of its parents as changed
		inline void MarkNodeAndParentsChanged(uint32 inNodeIndex);

		/// Widen parent bounds of node inNodeIndex to encapsulate inNewBounds, also mark node and all of its parents as changed
		inline void WidenAndMarkNodeAndParentsChanged(uint32 inNodeIndex, const AABox& inNewBounds);

		/// Allocate a new node
		inline uint32 AllocateNode(bool inIsChanged);

		/// Try to insert a new leaf to the tree at inNodeIndex
		inline bool TryInsertLeaf(TrackingVector& ioTracking, int inNodeIndex, NodeID inLeafID,
		                          const AABox& inLeafBounds, int inLeafNumBodies);

		/// Try to replace the existing root with a new root that contains both the existing root and the new leaf
		inline bool TryCreateNewRoot(TrackingVector& ioTracking, atomic<uint32>& ioRootNodeIndex, NodeID inLeafID,
		                             const AABox& inLeafBounds, int inLeafNumBodies);

		/// Build a tree for ioBodyIDs, returns the NodeID of the root (which will be the ID of a single body if inNumber = 1). All tree levels up to inMaxDepthMarkChanged will be marked as 'changed'.
		NodeID BuildTree(const BodyVector& inBodies, TrackingVector& ioTracking, NodeID* ioNodeIDs, int inNumber,
		                 uint inMaxDepthMarkChanged, AABox& outBounds);

		/// Sorts ioNodeIDs spatially into 2 groups. Second groups starts at ioNodeIDs + outMidPoint.
		/// After the function returns ioNodeIDs and ioNodeCenters will be shuffled
		static void sPartition(NodeID* ioNodeIDs, Vec3* ioNodeCenters, int inNumber, int& outMidPoint);

		/// Sorts ioNodeIDs from inBegin to (but excluding) inEnd spatially into 4 groups.
		/// outSplit needs to be 5 ints long, when the function returns each group runs from outSplit[i] to (but excluding) outSplit[i + 1]
		/// After the function returns ioNodeIDs and ioNodeCenters will be shuffled
		static void sPartition4(NodeID* ioNodeIDs, Vec3* ioNodeCenters, int inBegin, int inEnd, int* outSplit);

#ifdef JPH_DEBUG
	/// Validate that the tree is consistent.
	/// Note: This function only works if the tree is not modified while we're traversing it.
	void						ValidateTree(const BodyVector &inBodies, const TrackingVector &inTracking, uint32 inNodeIndex, uint32 inNumExpectedBodies) const;
#endif

#ifdef JPH_DUMP_BROADPHASE_TREE
	/// Dump the tree in DOT format (see: https://graphviz.org/)
	void						DumpTree(const NodeID &inRoot, const char *inFileNamePrefix) const;
#endif

		/// Allocator that controls adding / freeing nodes
		Allocator mAllocator;
		std::shared_ptr<PDTH::MemoryPoolPool> mPoolAgentSP;

		/// Number of bodies currently in the tree
		/// This is aligned to be in a different cache line from the `Allocator` pointer to prevent cross-thread syncs
		/// when reading nodes.
		alignas(JPH_CACHE_LINE_SIZE) atomic<uint32> mNumBodies{0};
	
		atomic<uint32> mRootNodeIndex{0};

		/// Flag to keep track of changes to the broadphase, if false, we don't need to UpdatePrepare/Finalize()
		atomic<bool> mIsDirty = false;
	

		/// Debug function to get the depth of the tree from node inNodeID
		uint GetMaxTreeDepth(const NodeID& inNodeID) const;

		/// Walk the node tree calling the Visitor::VisitNodes for each node encountered and Visitor::VisitBody for each body encountered
		template <class Visitor>
		JPH_INLINE void WalkTree(const ObjectLayerFilter& inObjectLayerFilter, const TrackingVector& inTracking,
		                         Visitor& ioVisitor JPH_IF_TRACK_BROADPHASE_STATS(, LayerToStats &ioStats)) const;

	};

JPH_NAMESPACE_END

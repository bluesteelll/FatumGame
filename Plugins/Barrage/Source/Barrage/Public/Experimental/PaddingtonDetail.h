#pragma once
#include "Memory/IntraTickThreadblindAlloc.h"

PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
JPH_NAMESPACE_BEGIN

class PDTH
{
public:
	static constexpr uint32 cInvalidNodeIndex = 0xffffffff; ///< Value used to indicate node index is invalid

	class NodeID
	{
	public:
		JPH_OVERRIDE_NEW_DELETE
		/// Default constructor does not init
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

		friend class AtomicNodeID;

		inline explicit NodeID(uint32 inID) : mID(inID)
		{
		}

		static const uint32 cIsNode = BodyID::cBroadPhaseBit;
		///< If this bit is set it means that the ID refers to a node, otherwise it refers to a body

		uint32 mID;
	};


	/// A NodeID that uses atomics to store the value
	class AtomicNodeID
	{
	public:
		/// Constructor
		AtomicNodeID() = default;

		explicit AtomicNodeID(const NodeID& inRHS) : mID(inRHS.mID)
		{
		}

		/// Assignment
		inline void operator =(const NodeID& inRHS) { mID = inRHS.mID; }

		/// Getting the value
		inline operator NodeID() const { return NodeID(mID); }

		/// Check if the ID is valid
		inline bool IsValid() const { return mID != PDTH::cInvalidNodeIndex; }

		/// Comparison
		inline bool operator ==(const BodyID& inRHS) const { return mID == inRHS.GetIndexAndSequenceNumber(); }
		inline bool operator ==(const NodeID& inRHS) const { return mID == inRHS.mID; }

		/// Atomically compare and swap value. Expects inOld value, replaces with inNew value or returns false
		inline bool CompareExchange(NodeID inOld, NodeID inNew)
		{
			return mID.compare_exchange_strong(inOld.mID, inNew.mID);
		}

	private:
		atomic<uint32> mID;
	};

	class Node
	{
	public:
		/// Construct node
		explicit Node(bool inIsChanged);

		/// Get bounding box encapsulating all children
		void GetNodeBounds(AABox& outBounds) const;

		/// Get bounding box in a consistent way with the functions below (check outBounds.IsValid() before using the box)
		void GetChildBounds(int inChildIndex, AABox& outBounds) const;

		/// Set the bounds in such a way that other threads will either see a fully correct bounding box or a bounding box with no volume
		void SetChildBounds(int inChildIndex, const AABox& inBounds);

		/// Invalidate bounding box in such a way that other threads will not temporarily see a very large bounding box
		void InvalidateChildBounds(int inChildIndex);

		/// Encapsulate inBounds in node bounds, returns true if there were changes
		bool EncapsulateChildBounds(int inChildIndex, const AABox& inBounds);

		/// behold a malevolent kingdom, where wrath is the sole offering.

		//bounds are defined as offsets onto the radius from the center.
		//in other words, a paddington node is a voxelized space of 256^3 blocks. This sounds bad but that's
		//actually a quite surprising amount of precision. Right now, for ease of implementation, we provide
		//the center with the node, but there's a way to more tightly encode this given that you have a link
		//to the same info about the parent AND where you fit in it. We really don't want to use that, though.
		//So why the FUDGE do it this way?
		//Three reasons: insanely small, insanely fast, insanely easy to reason about.
		//That.... last one is sort of a lie, though, if I'm honest. Cause. Um. See.....
		//Paddington nodes have a weird kind-of advantage. 
		char mBoundsMinX[4];
		char mBoundsMinY[4];
		char mBoundsMinZ[4];
		char mBoundsMaxX[4];
		char mBoundsMaxY[4];
		char mBoundsMaxZ[4];
		char mIsChanged = 0;
		/// Index of child node or body ID.
		AtomicNodeID mChildNodeID[4];

		/// Index of the parent node.
		/// Note: This value is unreliable during the UpdatePrepare/Finalize() function as a node may be relinked to the newly built tree.
		atomic<uint32> mParentNodeIndex = cInvalidNodeIndex;
		float xmin = 0;
		float ymin = 0;
		float zmin = 0;
		float radius = 0;
	};

	using Allocator = IntraTickThreadblindAlloc<Node>;

	class MemoryPoolPool
	{
		//i know, I know, okay?
		
	};
	
	/// Class that represents a node in the tree
	class RootNode
	{
	public:
		/// Construct node
		explicit RootNode(bool inIsChanged);

		/// Get bounding box encapsulating all children
		void GetNodeBounds(AABox& outBounds) const;

		/// Get bounding box in a consistent way with the functions below (check outBounds.IsValid() before using the box)
		void GetChildBounds(int inChildIndex, AABox& outBounds) const;

		/// Set the bounds in such a way that other threads will either see a fully correct bounding box or a bounding box with no volume
		void SetChildBounds(int inChildIndex, const AABox& inBounds);

		/// Invalidate bounding box in such a way that other threads will not temporarily see a very large bounding box
		void InvalidateChildBounds(int inChildIndex);

		/// Encapsulate inBounds in node bounds, returns true if there were changes
		bool EncapsulateChildBounds(int inChildIndex, const AABox& inBounds);

		/// behold a malevolent kingdom, where wrath is the sole offering.

		//bounds are defined as offsets onto the radius from the center.
		//in other words, a paddington node is a voxelized space of 256^3 blocks. This sounds bad but that's
		//actually a quite surprising amount of precision. Right now, for ease of implementation, we provide
		//the center with the node, but there's a way to more tightly encode this given that you have a link
		//to the same info about the parent AND where you fit in it. We really don't want to use that, though.
		//So why the FUDGE do it this way?
		//Three reasons: insanely small, insanely fast, insanely easy to reason about.
		//That.... last one is sort of a lie, though, if I'm honest. Cause. Um. See.....
		//Paddington nodes have a weird kind-of advantage. 
		char mBoundsMinX[64] = {};
		char mBoundsMinY[64] = {};
		char mBoundsMinZ[64] = {};
		char mBoundsMaxX[64] = {};
		char mBoundsMaxY[64] = {};
		char mBoundsMaxZ[64] = {};
		char mIsChanged = 0;
		/// Index of child node or body ID.
		/// If you have a small set, you should not have come here. Be glad and be gone. This is a forsaken land.
		Node mChildNodeID[64] = {};

		/// Index of the parent node.
		/// Note: This value is unreliable during the UpdatePrepare/Finalize() function as a node may be relinked to the newly built tree.
		atomic<uint32> mParentNodeIndex = cInvalidNodeIndex;
		double xmin = 0;
		double ymin = 0;
		double zmin = 0;
		double xmax = 0;
		double ymax = 0;
		double zmax = 0;
	};
};
JPH_NAMESPACE_END
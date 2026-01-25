//
//  config.h
//
//

#pragma once
//set up sane defaults for UE usage -JMK
#define SPATIAL_TREE_USE_CPP11
#define SPATIAL_TREE_STD_ALLOCATOR 1
#define SPATIAL_TREE_DEFAULT_ALLOCATOR 2

//if we see good initial performance with this,
//we'll want to add generational arena alloc
//as well as support for the weird jolt blob allocator.
#ifndef SPATIAL_TREE_ALLOCATOR
#define SPATIAL_TREE_ALLOCATOR 1
#endif

/// Preprocessor helper macros
#define SPATIAL_TREE_STRINGIFY(x) #x
#define SPATIAL_TREE_TOSTRING(x) SPATIAL_TREE_STRINGIFY(x)
#define SPATIAL_TREE_TOKENPASTE(x, y) x##y
#define SPATIAL_TREE_TOKENPASTE2(x, y) SPATIAL_TREE_TOKENPASTE(x, y)

namespace spatial {
	namespace detail {
		struct dummy_iterator {
			dummy_iterator &operator++() { return *this; }
			dummy_iterator &operator*() { return *this; }
			template <typename T> dummy_iterator &operator=(const T &) { return *this; }
		};

#if SPATIAL_TREE_ALLOCATOR == SPATIAL_TREE_STD_ALLOCATOR
		template <class AllocatorClass>
		inline typename AllocatorClass::value_type *allocate(AllocatorClass &allocator,
			int level) {
			typedef typename AllocatorClass::value_type Node;
			Node *p = allocator.allocate(1);
			// not using construct as deprecated from C++17
			new (p) Node(level);
			return p;
		}

		template <class AllocatorClass>
		inline void deallocate(AllocatorClass &allocator,
			typename AllocatorClass::value_type *node) {
			allocator.deallocate(node, 1);
		}
#elif SPATIAL_TREE_ALLOCATOR == SPATIAL_TREE_DEFAULT_ALLOCATOR
		template <class AllocatorClass>
		inline typename AllocatorClass::value_type *allocate(AllocatorClass &allocator,
			int level) {
			return allocator.allocate(level);
		}

		template <class AllocatorClass>
		inline void deallocate(AllocatorClass &allocator,
			typename AllocatorClass::value_type *node) {
			allocator.deallocate(node);
		}
#endif

		template <bool> struct static_assertion;
		template <> struct static_assertion<true> {};
	} // namespace detail
} // namespace spatial

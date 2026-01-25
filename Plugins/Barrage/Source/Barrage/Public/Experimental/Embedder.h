//Fast Line-Embedding Sketch Hasher implementation
//MIT License
//FLINNG originally copyright (c) 2021 Joshua Engels
//Sketch is by dnbaker.
//FLESH copyright 2025, Oversized Sun
//https://www.youtube.com/watch?v=vUJEG3tUVaY
#pragma once
#include "FakeRandom.h"
#include "LocomoUtil.h"
#include "Structures/flat_hash_map.hpp"
#include "Structures/MaybeTable.h"
#include "Structures/PascalCircularBuffer.h"

THIRD_PARTY_INCLUDES_START
PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
//THE FLESH DESIRES AN ARENA.
//THE ARENA DESIRES AN ALLOCATOR.
//okay technically, TLSF isn't an arena allocator, but we actually use it much like one.
//The Embedder's FLESH data structure is basically a very large inverted lookup based on FLINNG.
//It does a HUGE sweep of allocations but never modifies itself. So we want to be able to blow it up
//in a single call. That pretty much means a linear, pool, or arena allocator. Unfortunately,
//we don't have a great idea of what size one of the vector components is. So we allocate a large pool
//then use TLSF to manage it, basically creating arena regions representing each of the FLESH structures.
#include "IsolatedJoltIncludes.h"
#include "ZOrderDistances.h"
#include "Grouping/sketch/bbmh.h"
#include <memory>
typedef FZOrderDistances::FPLEmbed FLESHPoint;
THIRD_PARTY_INCLUDES_END

//FLINNG-driven index for point/line embeddings. See the FPLEmbed implementation for links to the
//paper that inspired it. These high-d vectors have most of the properties we would associate with
//sparse data, so FLineESH defaults to something close to the sparse model laid out in the FLINNG readme.
//Sketch provides our hasher.
template <uint32_t num_rows>
//see algorithm 1 from https://arxiv.org/pdf/2106.11565 (pg 4-5)//bit width per hash, basically.

class Embedder
{
public:
	using CDVBackingType = uint64_t;
	using CDVResultType = uint32_t; //could prolly go to 16...
	using CHAOSDUNKER = sketch::BBitMinHasher<CDVBackingType>; //cause it's time to play BBitball.
	using HASHER = CHAOSDUNKER; //fine. I'll play along. 
	using ALLOC_INNER = DefaultAwareIntraTickAlloc<uint32_t>; //you might notice this one's.... a lil different.
	//see https://stackoverflow.com/questions/21028299/is-this-behavior-of-vectorresizesize-type-n-under-c11-and-boost-container/21028912#21028912
	//for more info. This helps enable the horrible little resize trick we use. I think I need to expand it to dealloc too...

	using MAP_PAIR = std::pair<uint32_t, uint32_t>;
	using ALLOC_FIN = DefaultAwareIntraTickAlloc<MAP_PAIR>;
	using CellType = FPascally_15;
	using IndexType = FPascally_31;
	using Alloc_Cells = DefaultAwareIntraTickAlloc<CellType>;
	using Alloc_Ind = DefaultAwareIntraTickAlloc<IndexType>;
	

	//Normally, I'd make this a parametric as a template arg but under the hood, Sketch packs the bits of the hash
	//and doesn't expose the raw hashes. There's not really normally a reason to for their use case.
	//So you'd need a number of hash tables that is a multiple of 4. 8 hash tables is A LOT. so 4 it is.
	constexpr static uint8_t hash_range_pow = 16;
	constexpr static uint32_t num_hash_tables = 4;
	//   _   _   _   _
	constexpr static uint64_t mask = 0x000000000000FFFF;
	constexpr static uint32_t hash_range = (1 << hash_range_pow);
	constexpr static uint32_t table_width = (hash_range * num_hash_tables) + 1;
	//initial bytes of the sketch, permutes? it looks like it's permutes? I'm not sure. apparently this can one-perm????

	//previously, moving this into the inner class caused some template whackiness due to default constructors.
	//we've switched away from that allocator, due to other issues around memory overhead. I still wouldn't move this in
	//as it actually does make more sense out here. It's not really part of the actual machinery.
	struct HashBlob
	{
		CDVResultType Blob[num_hash_tables];
	};

	static HashBlob& getHash(FLESHPoint& data,
	                         uint32_t seed)
	{
		thread_local HashBlob ret;
		alt_dense_hash(ret.Blob, data);
		return ret;
	}

	static void alt_dense_hash(CDVResultType* result, FLESHPoint& data)
	{
		thread_local HASHER BBitMinHashInst = HASHER(log2(num_hash_tables), hash_range_pow);
		BBitMinHashInst.addh(data.voxeldata[0]);
		BBitMinHashInst.addh(data.voxeldata[1]);
		BBitMinHashInst.addh(data.voxeldata[2]);
		BBitMinHashInst.densify();//do we needdddddddddd this?!

		auto Res = BBitMinHashInst.core();
		for (int BitPackedBin = 0; BitPackedBin < num_hash_tables / 4; ++BitPackedBin)
		{
			for (int UnpackOffset = 0; UnpackOffset < 4; ++UnpackOffset) // unpackku
			{
				result[BitPackedBin + UnpackOffset] = (Res[BitPackedBin]
						>> (hash_range_pow * UnpackOffset))
					& mask;
			}
		}
		BBitMinHashInst.reset();
		//now what???
	}

	class FLESH
	{
	public:
		using Arena = IntraTickThreadblindAlloc<uint32_t>::TrueLifecycleManager;

	private:
		using RawPool = IntraTickThreadblindAlloc<uint32_t>::POOL*;
		using Cells = std::vector<CellType, Alloc_Cells>;
		using InvertInd = std::vector<IndexType, Alloc_Ind>;
		using MaybeTable = FMaybeTable;
		ALLOC_INNER ints_alloc = ALLOC_INNER(); //INTS ALLOC IS A BLIND CHILD.
		ALLOC_FIN pair_alloc = ALLOC_FIN(); //INTS ALLOC IS A BLIND CHILD.
		Alloc_Cells cell_alloc = Alloc_Cells(); //THIS ISN'T A METAPHOR. it uses vecs alloc's pool.
		bool IsReady = false;
		Alloc_Ind ind_alloc = Alloc_Ind(); //THIS ISN'T A METAPHOR. it uses vecs alloc's pool.
		uint64_t cells_per_row;
		uint64_t total_points_added = 0;
		bool IHaveAPool = false;
		static constexpr size_t PoolSize = 64 * 1024 * 1024;
		RawPool ForMemsetClear = nullptr;
		//so I tried to make this an std::array and it causes some templating fun. ultimately, I went with vector+reserve.
		//this is for legibility reasons and because I think I'll need to structurally rework the entire design ove the invert index, tbh.
		Cells* cell_membership;
		InvertInd* inverted_flinng_index;

	public:
		Arena INTERNAL_ARENA;

		FLESH(): cells_per_row(0), cell_membership(nullptr), inverted_flinng_index(nullptr)
		{
		}

		//the act of unknowing is never safe.
		//it is often necessary. remember, the buddha is a wooden duck.
		~FLESH()
		{
			if (IHaveAPool && INTERNAL_ARENA) // if I think I have a pool and I still hold a strong ref to the arena
			{
				memset(ForMemsetClear, 0, PoolSize);
			}
		}

		// This is the main entry point for querying.
		std::vector<uint32_t> query(HashBlob& hash, uint32_t top_k)
		{
			std::vector<uint32_t> results(top_k);
			MaybeTable counts;
			DefaultBehavior A(results, hash);
			ProcessQueryBlob(A, top_k, &counts);
			return results;
		}

		//Heyyyyy sooooooo..... Don't ever do this. We actually _clear the memory pool itself_ so we don't want to call dealloc on these guys.
		//but you really really really can't do this unless you are only using trivially destructible types, and really, that basically means
		//primitives in most modern applications. Anyway, enjoy this bit of incredibly horrible arcana. It makes things _asymptotically_ faster.
		//No, I don't like that either. Practically, it's a huge speed up as well.
		void Build(uint64_t DataSize)
		{
			cell_membership = new Cells(num_rows * DataSize, cell_alloc);
			inverted_flinng_index = new InvertInd(table_width,  ind_alloc);
			ForMemsetClear = cell_alloc.InitialPool;//we don't use it but we do save it.
		}

		//when flesh is destroyed, it memsets the pool but DOES NOT DESTROY IT unless you discard that out param.
		//it will do the right thing either way, functionally.
		FLESH(uint64_t DataSize, Arena& OUT_PARAM_ARENALIFECYCLE)
			: cells_per_row(std::max(DataSize, 1ull)), //c'mon. man.
			  cell_alloc(PoolSize)
		{
			INTERNAL_ARENA = cell_alloc.Init();
			total_points_added = 0;
			//DO NOT COMBINE WITH BELOW. the currying works in an odd way due to inlining or SOMETHING.
			//look, if you so badly want to save zero operations, because the compiler ALMOST CERTAINLY optimizes this, it'd actually make me really happy.
			//oh, thought I'd be snarky? no. This is like a bur I can't polish out. We just don't have time right now, and it's honestly a little clearer this way
			//but it doesn't quite match the usual allocate-at-use style we prefer. in fact, none of this is very RAII.
			ints_alloc = ALLOC_INNER(cell_alloc.UnderlyingTlsf, cell_alloc.InitialPool, cell_alloc.Size);
			pair_alloc = ALLOC_FIN(cell_alloc.UnderlyingTlsf, cell_alloc.InitialPool, cell_alloc.Size);
			ind_alloc = Alloc_Ind(cell_alloc.UnderlyingTlsf, cell_alloc.InitialPool, cell_alloc.Size);

			Build(DataSize);
			OUT_PARAM_ARENALIFECYCLE = INTERNAL_ARENA;
			IHaveAPool = true;
		}

		//consign what you were to the void.
		void RegenerateTheFLESH(uint64_t DataSize)
		{
			cell_alloc.DoNotUseThis();
			total_points_added = 0;
			cells_per_row = std::max(DataSize, 1ull);
			ints_alloc = ALLOC_INNER(cell_alloc.UnderlyingTlsf, ints_alloc.InitialPool, cell_alloc.Size);
			pair_alloc = ALLOC_FIN(cell_alloc.UnderlyingTlsf, ints_alloc.InitialPool, cell_alloc.Size);

			Build(DataSize);
			IsReady = true;
		}

		std::vector<HashBlob> getHashes(std::vector<FLESHPoint> data,
		                                uint32_t seed)
		{
			return densified_minhash(data, seed);
		}

		std::array<HashBlob, 2> get2Hashes(FLESHPoint* data,
		                                    uint32_t seed)
		{
			return densified_minhash<2>(data, seed);
		}

		//this just abstracts away the particulars of hashing a little. :/
		//if you need to switch back to dense mode, it'll be nice to have.
		inline std::vector<HashBlob>
		densified_minhash(std::vector<FLESHPoint>& points, uint32_t random_seed)
		{
			std::vector<HashBlob> result(points.size());
			for (uint64_t point_id = 0; point_id < points.size(); point_id += 1)
			{
				alt_dense_hash(result[point_id].Blob,
				               points[point_id]);
			}
			return result;
		}

		template <int FixedPointCount>
		inline std::array<HashBlob, FixedPointCount>
		densified_minhash(FLESHPoint* points, uint32_t random_seed)
		{
			std::array<HashBlob, FixedPointCount> result = {};
			for (uint64_t point_id = 0; point_id < FixedPointCount; ++point_id)
			{
				alt_dense_hash(result[point_id].Blob,
				               points[point_id]);
			}
			return result;
		}


		////TABLE funcs

		// These points are added to different cells and groups, and hashed to different spots
		// but they are not actually treated as unique points.
		void addFoldedHashedPoints(HashBlob* hashes, uint64_t num_points, uint32_t seed)
		{
			for (uint64_t table = 0; table < num_hash_tables; table++)
			{
				for (uint64_t point = 0; point < num_points; point++)
				{
					auto hash = hashes[point].Blob[table];

					uint32_t hash_id = (table * hash_range) + hash;
					for (uint64_t row = 0; row < num_rows; row++)
					{
						auto rnd_cel = std::max(1ull,
																			 ( //generate an offset unique per point and per row.
																				 (FMMM::FastHash32(row + seed) + (
																						 FMMM::FastHash32(
																							 total_points_added + point
																							 + seed))
																					 % cells_per_row + cells_per_row)
																				 //adds a littttle entropy in the ring-wrap case. maybe superstition?
																				 % cells_per_row) //wrap to cell count.
																			 + (row * cells_per_row));
						//it's faster to recalculate this than to build a vector. by an _uncomfortable_ margin.
						bool added = (*inverted_flinng_index)[hash_id].push_back(rnd_cel);
						if (!added)
						{
							auto probe = hash_id+(*inverted_flinng_index)[hash_id].cycle % ((num_hash_tables*hash_range)-2);
							(*inverted_flinng_index)[probe].push_back(rnd_cel);
						}
					}
				}
			}

			if ((*cell_membership).size() > 0)
			{
				for (uint64_t point = 0; point < num_points; point++)
				{
					for (uint64_t row = 0; row < num_rows; ++row)
					{
						const auto idx = ((point * num_rows) + row) + total_points_added;
						if (idx <= (*cell_membership).size())
						{
							 bool added = (*cell_membership)[idx].push_back(
								total_points_added);
							if (!added)
							{
								std::cerr << "Now we shall pay for my arrogance." << std::endl;
								(*cell_membership)[idx].push_back(
								total_points_added);
							}
						}
						else
						{
							throw; //temp
						}
					}
				}
			}

			++total_points_added;
		}
		


		struct DefaultBehavior
		{
			static inline HashBlob DefaultBlob = HashBlob();
			std::vector<uint32_t>& results;

			DefaultBehavior(std::vector<uint32_t>& Results, HashBlob& hash)
				: results(Results),
				  hash(hash)
			{
			}

			DefaultBehavior(std::vector<uint32_t>& Results)
				: results(Results),
				  hash(DefaultBlob)
			{
			}


			HashBlob& hash;

			bool AttemptAdd(uint32_t IdxToAdd)
			{
				results.push_back(IdxToAdd);
				return true;
			};
		};

		template <class behavior = DefaultBehavior>
		bool ProcessQueryBlob(behavior& BoundQuery, uint32_t top_k,
		                      MaybeTable* counts)
		{
			uint16_t num_found = 0;
			for (uint32_t rep = 0; rep < num_hash_tables; rep++)
			{
				uint32_t index = hash_range * rep + BoundQuery.hash.Blob[rep];
				//fun story, these are NOT the original indexes. These are cell memberships. I'm not really sure
				//at this point that flinng adds ANYTHING over a standard LSH index.
				for (int invert = 0; invert < (*inverted_flinng_index)[index].count; ++invert)
				{
					auto k = (*inverted_flinng_index)[index].block[invert];
					if (num_found >= top_k)
					{
						return true;
					}
					if (k != 0)
					{
						auto itty = counts->CountNoParityCheck(k);

						if (itty == (num_hash_tables * num_rows) - 1u || itty == (num_hash_tables * num_rows)) //blyat.
						{
							auto& bind = (*cell_membership)[invert];
							for (int i = 0; i < bind.count && num_found < top_k; ++i)
							{
								//std::cerr << bind[i] << "..." << std::endl;
								num_found +=
									BoundQuery.AttemptAdd(bind.block[i]); //only monch what may moncheth be.
							}
						}
					}
				}
			}
			if (num_found >= top_k)
			{
				return true;
			}
			return false;
		}

		// Again all the hashes for point 1 come first, etc.
		// Size of hashes should be multiple of num_hash_tables
		// Results are similarly ordered
		std::vector<uint32_t> query(HashBlob* hashes, size_t queries, uint32_t top_k)
		{
			std::vector<uint32_t> results(top_k * queries);
			ApplyBehaviorAndQuery(hashes, top_k, queries, results);
			return results;
		}

		//must be hashes.len*top_k long.
		std::vector<uint32_t> query_byoa(HashBlob* hashes, size_t queries, uint32_t top_k,
		                                 std::vector<uint32_t>& results)
		{
			ApplyBehaviorAndQuery<DefaultBehavior>(hashes, top_k, queries, results);

			return results;
		}

		//must be hashes.len*top_k long.

		template <class QueryBehavior>
		bool query_byob(HashBlob* hashes, size_t queries, uint32_t top_k,
		                QueryBehavior& Behavior)
		{
			ApplyBehaviorAndQuery<QueryBehavior>(hashes, top_k, queries, Behavior);
			return true;
		}

		template <class QueryBehavior = DefaultBehavior>
		void ApplyBehaviorAndQuery(HashBlob* hashes, uint32_t top_k, uint64_t num_queries, QueryBehavior& results)
		{
			for (uint32_t query_id = 0; query_id < num_queries; query_id++)
			{

				MaybeTable counts;
				_mm_prefetch(reinterpret_cast<char const*>(&counts), _MM_HINT_T1);
				//this does _next to nothing_ but hey. good luck charm.
				results.hash = hashes[query_id];
				ProcessQueryBlob<QueryBehavior>(results, top_k, &counts);
				//std::cerr << counts.size() << " BBBEEEG" << std::endl;
			}
		}

		template <class QueryBehavior = DefaultBehavior>
		void ApplyBehaviorAndQuery(HashBlob* hashes, uint32_t top_k, uint64_t num_queries,
		                           std::vector<uint32_t>& results)
		{
			auto A = QueryBehavior(results);
			ApplyBehaviorAndQuery<QueryBehavior>(hashes, top_k, num_queries, A);
		}
	};
};

PRAGMA_POP_PLATFORM_DEFAULT_PACKING

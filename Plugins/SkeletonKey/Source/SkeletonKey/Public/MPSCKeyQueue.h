#pragma once
#include <atomic> 

#include "SkeletonTypes.h"
#define ACC___MAXT 16
#define ACC___THREADRING ACC___MAXT -1
#define ACC___HEAD 7
#define ACC___CHILD 63
#define ACC___WOODSWIDTH 4

//atomic is vastly more powerful and effective than the UE atomics, which they deprecated.
//DO NOT use the UE atomics. DO NOT DO IT.
//Only use the platform atomics or the standard <atomic>.

//INSPIRED by https://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
//what in gods name is this.

//unfair _FILO_ lockless mpsc horror. max width of producers is 255. please don't need more than that.
struct slink_node_t
{
	//by using memory blocks, we can trivially eliminate atomic behavior. the indexes would be agonizing otherwise.
	FSkeletonKey key[ACC___HEAD];
	uint32 Up;
	uint32 Down;
};

class KeySlink
{
public:
	
	thread_local static uint8 AmIHere;
	struct mpscq_node_t
	{
		mpscq_node_t* volatile  next = nullptr;
		FSkeletonKey key = FSkeletonKey();
	};

	struct mpscq_t
	{
		std::atomic<mpscq_node_t*> head;
		mpscq_node_t*           tail;
		mpscq_node_t            stub;
	};
	
	mpscq_t self;
	void mpscq_push(mpscq_node_t* n);

	mpscq_node_t* mpscq_pop();

	struct GrowOnlyList
	{
		std::atomic<uint8> consumerindex = 0; //claim
		//this uses a set of what are effectively forward only latches that guarantee no-interstitials
		//and since they only ever get set once, and the initialization step is a thread_local...
		bool slink[ACC___MAXT]; //this only ever gets set, so we're safe here for horrifying reasons.
		slink_node_t slonk[ACC___MAXT]; //sorry, I'm not supporting more than 64 threads. I'm just not.
	};
	uint8 WorkingOn = 0;//the current operation can be deduced by checking workingonnext.
	bool AmReady = false;
	GrowOnlyList growonly;

	KeySlink();

	void ThreadInit();

	//false just means we pushed to the queue.
	bool Push(FSkeletonKey key);
	FSkeletonKey PopOverflow(bool& stop);


	FSkeletonKey Pop(bool& stop);

private:
	void Mpscq_Create();
};

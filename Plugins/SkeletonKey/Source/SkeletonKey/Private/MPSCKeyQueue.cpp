#include "MPSCKeyQueue.h"

thread_local uint8 KeySlink::AmIHere = 0;

void KeySlink::mpscq_push(mpscq_node_t* n)
{
	n->next = 0;
	mpscq_node_t* prev = self.head.exchange(n);
	prev->next = n;
}

KeySlink::mpscq_node_t* KeySlink::mpscq_pop()
{
	mpscq_node_t* tail = self.tail;
	mpscq_node_t* next = tail->next;
	if (tail == &self.stub)
	{
		if (0 == next)
		{
			return 0;
		}
			
		self.tail = next;
		tail = next;
		next = next->next;
	}

	if (next)
	{
		self.tail = next;
		return tail;
	}
	mpscq_node_t* head = self.head;
	if (tail != head)
	{
		return 0;
	}
	mpscq_push(&self.stub);
	next = tail->next;
	if (next)
	{
		self.tail = next;
		return tail;
	}
	return 0;
}

KeySlink::KeySlink(): self(), growonly()
{
	for (int i = 0; i < ACC___MAXT; i++)
	{
		//we could template up an init list but that looks satanic.
		growonly.slonk[i].key[0] = 0;
		growonly.slonk[i].Up = 0;
		growonly.slonk[i].Down = 0;
		growonly.slink[i] = false;
	}
	Mpscq_Create();
	AmReady = true;
}

//this is not idempotent. because I'm exhausted and I don't think we'll be using this class.
void KeySlink::ThreadInit()
{
		auto loc = growonly.consumerindex.fetch_add(1, std::memory_order_seq_cst); //this can be slow, brudder
		growonly.slink[loc] = true;
		AmIHere = loc;
}

bool KeySlink::Push(FSkeletonKey key)
{
	
	auto& A = growonly.slonk[AmIHere];
	auto Left = A.Down % ACC___HEAD; //this records where we think the head is.
	auto Right = A.Up % ACC___HEAD;
	if (A.Up == A.Down)
	{
		//empty. this case is easy. the write head is either at or done with the last thing we wrote.
		A.key[(A.Up + 1) % ACC___HEAD] = key;
		A.Up = A.Up + 1; // we step the write head forward.
		return true;
	}
	if (Left - Right <= 1) // we're too close to a stomp, push to the other queue. if we're wrong, no harm done.
	{
		auto nNode = new mpscq_node_t();
		nNode->key = key;
		mpscq_push(nNode);
		return false;
	}
	A.key[(A.Up + 1) % ACC___HEAD] = key;
	++A.Up; // we step the write head forward.
	return true;
}
FSkeletonKey KeySlink::PopOverflow(bool& stop)
{
	auto get = mpscq_pop();
	stop = true;
	if (get != nullptr)
	{
		stop = false;
		auto mvKey = get->key;
		delete get;
		return mvKey;
	}
	return FSkeletonKey();	
}

//you probably should keep going if pop returns a stop of false.
FSkeletonKey KeySlink::Pop(bool& stop)
{
	stop = false;
	WorkingOn =  (WorkingOn + 1) % ACC___MAXT ;
	auto& A = growonly.slonk[WorkingOn];
	auto Right = A.Up; //this records where we THINK the other head is +/- 1.
	
	if (Right == A.Down) //if this is the case, we don't step forward!!!!! we'll have to get anything we missed next time.
	{
		//either we're empty or we're in an inaccurate state.
		auto get = mpscq_pop();
		if (get != nullptr)
		{

			auto moveKey =get->key;
			delete get;
			return moveKey;
		}
		stop = true;
		return FSkeletonKey();
	}
	//there might be stuff in the overflow queue. we aren't checking yet.
	auto mvKey = A.key[Right % ACC___HEAD];
	++A.Down; // we step the read head forward.
	return mvKey;
}
void KeySlink::Mpscq_Create()
{
	self.head = &self.stub;
	self.tail = &self.stub;
	self.stub.next = nullptr;
}


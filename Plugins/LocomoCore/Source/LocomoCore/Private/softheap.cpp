/* File: softheap.c
 * Developer: Matt Millican, May 2016
 * ----------------------------------
 * Implementation of a soft heap. Rather than the version using binomial trees
 * introduced in Chazelle's original paper, this soft heap uses binary trees
 * according to the strategy outlined in Kaplan/Zwick, 2009.
 */

#include "Structures/softheap.h"

void FSoftheap::destroy_node(node* treenode)
{
	if (treenode == NULL) return;

	cell *curr = treenode->first, *next;
	while (curr != NULL)
	{
		next = curr->next;
		free(curr);
		curr = next;
	}

	destroy_node(treenode->left);
	destroy_node(treenode->right);
	free(treenode);
}

void FSoftheap::destroy_heap(softheap* P)
{
	if (P == NULL) return;

	tree *curr = P->first, *next;
	while (curr != NULL)
	{
		next = curr->next;
		destroy_node(curr->root);
		free(curr);
		curr = next;
	}

	free(P);
}

void FSoftheap::moveList(node* src, node* dst)
{
	if (dst->last != NULL) dst->last->next = src->first;
	else dst->first = src->first;
	dst->last = src->last;

	dst->nelems += src->nelems;
	src->nelems = 0;
	src->first = src->last = NULL;
}

void FSoftheap::sift(node* x)
{
	while (x->nelems < x->size && !leaf(x))
	{
		// For simplicity, switch left and right children so that left child exists & has smaller ckey
		if (x->left == NULL || (x->right != NULL && x->left->ckey > x->right->ckey)) swapLR(x);
		moveList(x->left, x); // concat left's list to x's to replenish x
		x->ckey = x->left->ckey;

		// if left was a leaf, it can't be repaired, so destroy it
		if (leaf(x->left))
		{
			free(x->left);
			x->left = NULL;
		}
		else
		{
			sift(x->left);
		}
	} // Repeat as necessary until x is repaired or until x is a leaf and no more repairs are possible
}

FSoftheap::node* FSoftheap::combine(node* x, node* y, int r)
{
	node* z = new node();
	z->left = x;
	z->right = y;
	z->rank = x->rank + 1;
	z->nelems = 0;
	z->first = z->last = NULL;

	z->size = get_next_size(z->rank, x->size, r);
	sift(z);
	return z;
}

void FSoftheap::insert_tree(softheap* into_heap, tree* inserted, tree* successor)
{
	inserted->next = successor;

	if (successor->prev == NULL) into_heap->first = inserted;
	else successor->prev->next = inserted;
	inserted->prev = successor->prev;
	successor->prev = inserted;
}

void FSoftheap::remove_tree(softheap* outof_heap, tree* removed)
{
	if (removed->prev == NULL) outof_heap->first = removed->next;
	else removed->prev->next = removed->next;
	if (removed->next != NULL) removed->next->prev = removed->prev;
}

void FSoftheap::update_suffix_min(tree* T)
{
	while (T != NULL)
	{
		if (T->next == NULL || T->root->ckey <= T->next->sufmin->root->ckey) T->sufmin = T;
		else T->sufmin = T->next->sufmin;
		T = T->prev;
	}
}

void FSoftheap::merge_into(softheap* P, softheap* Q)
{
	tree *currP = P->first, *currQ = Q->first;

	while (currP != NULL)
	{
		while (currQ->rank < currP->rank) currQ = currQ->next;
		// currQ is now the first tree in Q with rank >= currP. Insert currP before it.
		tree* next = currP->next;
		insert_tree(Q, currP, currQ);
		currP = next;
	}
}

void FSoftheap::repeated_combine(softheap* Q, int smaller_rank, int r)
{
	tree* curr = Q->first;

	while (curr->next != NULL)
	{
		bool two = (curr->rank == curr->next->rank);
		bool three = (two && curr->next->next != NULL && curr->rank == curr->next->next->rank);

		if (!two)
		{
			// only one tree of this rank
			if (curr->rank > smaller_rank) break; // no more combines to do and no carries
			else curr = curr->next;
		}
		else if (!three)
		{
			// exactly two trees of this rank
			// combine them to make a carry, then delete curr->next. 
			// carry may need to be merged with its next tree, so do not advance curr.
			curr->root = combine(curr->root, curr->next->root, r);
			curr->rank = curr->root->rank;
			tree* tofree = curr->next;
			remove_tree(Q, curr->next); // will change what curr->next points to
			free(tofree);
		}
		else
		{
			// exactly three trees of this rank
			// skip the first so that we can combine the second and third to form a carry
			curr = curr->next;
		}
	}

	if (curr->rank > Q->rank) Q->rank = curr->rank; // Q might have a new highest-rank tree
	update_suffix_min(curr); // this is final tree affected by merge, so update sufmin backwds from here
}

int FSoftheap::extract_elem(node* x)
{
	cell* todelete = x->first;
	int result = todelete->elem;

	x->first = todelete->next;
	if (x->first == NULL) x->last = NULL;
	else if (x->first->next == NULL) x->last = x->first;

	free(todelete);
	x->nelems--;
	return result;
}

bool FSoftheap::empty(softheap* P)
{
	return P->first == NULL;
}

void FSoftheap::insert(softheap* P, int elem)
{
	if (empty(P))
	{
		P->first = maketree(elem);
		P->rank = 0;
	}
	else meld(P, makeheap(elem, P->epsilon));
}

FSoftheap::softheap* FSoftheap::meld(softheap* P, softheap* Q)
{
	// Do not allow melding if the soft heaps don't seem to have the same error parameter.
	double max_eps = FMath::Max(P->epsilon, Q->epsilon), min_eps = min(P->epsilon, Q->epsilon);
	double eps_off = 1 - min_eps / max_eps;

	// If both softheaps empty, just destroy one and return the other
	if (empty(P) && empty(Q))
	{
		free(P);
		return Q;
	}

	softheap* result;
	if (P->rank >= Q->rank)
	{
		// meld Q into P
		merge_into(Q, P);
		repeated_combine(P, Q->rank, P->r);
		free(Q);
		result = P;
	}
	else
	{
		// meld P into Q
		merge_into(P, Q);
		repeated_combine(Q, P->rank, Q->r);
		free(P);
		result = Q;
	}

	return result;
}

int FSoftheap::extract_min_with_ckey(softheap* P, int* ckey_into)
{
	tree* T = P->first->sufmin; // tree with lowest root ckey
	node* x = T->root;
	int e = extract_elem(x);
	*ckey_into = x->ckey;

	if (x->nelems <= x->size / 2)
	{
		// x is deficient; rescue it if possible
		if (!leaf(x))
		{
			sift(x);
			update_suffix_min(T);
		}
		else if (x->nelems == 0)
		{
			// x is a leaf and empty; it must be destroyed
			free(x);
			remove_tree(P, T);

			if (T->next == NULL)
			{
				// we removed the highest-ranked tree; reset heap rank and clean up
				if (T->prev == NULL) P->rank = -1; // Heap now empty. Rank -1 is sentinel for future melds
				else P->rank = T->prev->rank;
			}

			if (T->prev != NULL) update_suffix_min(T->prev);
			free(T);
		}
	}

	return e;
}

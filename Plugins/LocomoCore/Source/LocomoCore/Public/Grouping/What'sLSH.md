LSH [Locality sensitive hashing] is a way of mapping similar things so that they're close together. it 
shares a lot of heritage with and often overlaps directly with the concept
of embeddings. They aren't entirely one-to-one, but you can think of an LSH 
as a hash function that produces an embedding vector, one that's often
bitonic. And like with many bitonic tools, they have some very cool properties.
Arena is untested. We only ended up using TLSF so far. While TLSF is very very fast, it's also very old. If you're 
for good allocation tools, I recommend RPMalloc or the C++20 allocator extensions. 

You can find the latter here: https://en.cppreference.com/w/cpp/memory/polymorphic_allocator.html
TLSFEW is a VERY RICKETY shim that presents TLSF as an allocator, and stands for TLSF Evil Wrapper.
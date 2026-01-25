Hi! This is a super super super stripped down subset of Dr. Baker's
EXCELLENT library Sketch, which for quite a long time was basically
best in class for both completeness and speed in terms of portable C++.
I'm not totally sure of the maintenance status of it at the moment, but
the entry point for us is the bbmh.h file, which presents the particular
tooling we ended up needing.

You can take a look at the original library here:   
https://github.com/dnbaker/sketch  

Here's an example of how you might use it:  
https://github.com/dnbaker/10xdash/blob/master/src/10xdash.cpp  

In general, Sketch is pretty overkill for a lot of what you'll end up doing.

HOWEVER.

It exists. It's tested. This trimmed down version is still very fast. I suggest
that you consider using it for applications centered hashing. It provides both LSH-like
capabilities but also packages up and exposes a number of classic hash functions as well 
as many that are a little bit more esoteric. Even in this thin slice of the library, you'll
find quite a bit on offer.

That said, this is some heavy shit. If you can get away with it, just use the interface.
If you have time to spare, though, it's lovely and engrossing code.
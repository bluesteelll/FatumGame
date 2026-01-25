A permissively licensed RTree. Largely untested. There were like forty other trees here but 
the only one that I think we have a good chance of bringing back is the MVP-tree and maybe my own
Turnwise Tree.

If anyone needs it, a TWT is a kind of prefix trie that uses multiple encodings. These turnwise
encodings uses a weird quirk of morton codes. Namely, their discontinuities are rotation sensitive. Thus, if you
rotate the full set according to a known quaternion and take the morton coding again, significant amounts of new information
are present. Like, much more than you'd expect. 
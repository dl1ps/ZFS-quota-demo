# ZFS-quota-demo
Democode showing how ZFS quota of a user is read using C and libzfs

The libzfs documentation is not easy to understand. For a project I 
needed to know how to use the libzfs library to read quota information
from a zfs filesystem with C. To avoid performance issues, I did not 
liked to use zfs command line tools.

This C file is a minimal example and can be compiled with the one-liner
outlined in the comments.

Maybe it is useful to other programmers.

References

https://despairlabs.com/blog/posts/2025-03-12-we-should-improve-libzfs-somewhat/

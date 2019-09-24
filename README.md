# pcache
PageCached

PageCached is used to count and analyze pages cached in the memory by the kernel of common files.
It can ADVISE the kernel to release/cache the specified file/directory.  Linux kernel 2.6 is required.
If a directory specified, all files in this directory will be processed (including subdirectories).
Hide files (start with .) will not processed.

Usage:
pcache [options] File/Directory

Options:

-c count the pages cached in memory. Page size is 4KBytes.

-v verbose output

-d advise kernel attempts to free cached pages associated with the specified region. Dirty pages that have not been synchronized may not work.

-n advise kernel the specified data will be accessed in the near future.The amount of data read may be decreased by the kernel depending on virtual memory load.

-h print this info.

Example:
Analyze the size of all files in the current directory (including subdirectories) that are cached into memory.

        pcache -c .

Analyze the size of all files in the /home/data/history (including subdirectories) that are cached into memory.And advise the kernel to release the cache of these files.

        pcache -cd /home/data/history

Advise the kernel do free the cache of all files in the /home/data (including subdirectories).

        pcache -d /home/data

Advise the kernel to cache all files in the /home/data (including subdirectories) into memory. This operation may cause HIGH system I/O!

        pcache -n /home/data

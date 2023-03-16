# dsk2nib
Apple II DSK-to-NIB and NIB-to-DSK image file conversion utilities.

Build
-----
Run `make clean all` to produce the `dsk2nib` and `nib2dsk`
executables. Use `make debug` to create debugging binaries, if desired.


Sample Usage
------------
Some Apple II games use the disk volume number to represent the disk number in a multi-disk set. The `dsk2nib` command is useful in this case, since the volume number is only present in a NIB image.

    dsk2nib shadowkeep4.dsk shadowkeep4.nib 4
    nib2dsk silicon.nib silicon.dsk

Note
----
All DSK files can be turned into NIBs, but not vice-versa. 

History
-------
I wrote these utilities in the early days of Apple II emulation, and only recently decided to make them buildable under macOS/Linux.

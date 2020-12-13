#Versioning Virtual File System (using FUSE)
============================================

Based on a passthrough file system (`mirrorfs.c`) code by Prof. Scott Kaplan.
Modified into a versioning file system by me.



Make
=====

`make clean` remove old binaries

`make versfs` will create the binary 


Mounting
=========

`./versfs <storage dir path> <mount point path>`

E.g., `./versfs ${PWD}/stg ${PWD}/mnt`


Unmounting
===========

`fusermount -u ${PWD}/mnt`



Dumping files
===============

`copy.sh` is my shell script for file dumping.
To run, enter `sh copy.sh` in bash and follow prompts.

Please note that you will get the current file:

`foo.txt`

Its previous versions:

`foo.txt,4`
`foo.txt,3`
`foo.txt,2`
`foo.txt,1`
`foo.txt,0`

where `foo.txt,4` is the youngest backup version, and `foo.txt,0` is the oldest backup version.

And the version metadata tracking file that contains the current backup version number (it's utilized by my *versfs* for better computational complexity -- a space-effiicient (only **4 bytes** per tracked file; naturally, it rounds up to the size of **1 block**) method to avoid parsing the directory every time to find out the highest version):

`foo.txt,v`




Design
======

1. Preserves previous versions of files before *vers_write()* and *vers_truncate()* get invoked.
	E.g., running `printf "lorem ipsum" > 1.txt` and then running `printf "dolor" > 1.txt` will create version points before truncate (file with "lorem ipsum") and before write (empty file after truncate).
2. Maintains metadata in the `*,v` file for better runtime complexity.
3. Hides previous versions and the metadata file. E.g., running `ls -l` in the `/mnt` directory will only show `foo.txt`.
4. Deletion of `foo.txt` (e.g., via `rm foo.txt`) deletes `foo.txt` and all its backup versions and the metadata file.
5. Working rename: Running the `mv 1.txt 2.txt` command will make my *versfs* parse and rename all the backup version files like so `1.txt,0` -> `2.txt,0`. Additionally, it will rename the metadata file accordingly.



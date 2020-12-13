CC          = gcc
DEBUG_FLAGS = -ggdb -Wall
CFLAGS      = `pkg-config fuse --cflags --libs` $(DEBUG_FLAGS)


versfs: versfs.c
	$(CC) $(CFLAGS) -o versfs versfs.c
	
mirrorfs: mirrorfs.c
	$(CC) $(CFLAGS) -o mirrorfs mirrorfs.c

clean:
	rm -f mirrorfs caesarfs versfs

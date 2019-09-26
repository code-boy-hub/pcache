all: pcache.c
	cc -Wall -o pcache -O3 -Os pcache.c
	strip pcache

clean:
	rm pcache


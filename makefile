all: pcache.c
	cc -o pcache -O3 -Os pcache.c
	strip pcache

clean:
	rm pcache

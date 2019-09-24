all: pcache.c
	cc -o pcache -O3 pcache.c
	strip pcache

clean:
	rm pcache

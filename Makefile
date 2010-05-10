twits: twits.c
	gcc -Wall -std=c99 -O2 -o twits `curl-config --cflags` `xml2-config --cflags` twits.c `curl-config --libs` `xml2-config --libs`

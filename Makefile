.PHONY: gigahertz

gigahertz:
	gcc -o gigahertz -lX11 -lXft -I/usr/include/freetype2 gigahertz.c


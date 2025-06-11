clemos: 
	mkdir mnt && gcc -Wall `pkg-config fuse --cflags` clemos.c -o clemos `pkg-config fuse --libs` && ./clemos mnt

down: 
	sudo umount mnt && rm clemos

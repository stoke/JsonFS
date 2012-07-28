all:
	gcc src/hello.c src/cJSON.c -o jsonmounter -D_FILE_OFFSET_BITS=64 -lfuse -lm -g

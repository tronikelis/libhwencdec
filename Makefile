.PHONY: encode
encode: encode.o
	./encode.o

encode.o: encode.c
	clang \
		encode.c \
		-I/opt/cuda/include \
		-lnvidia-encode \
		-lcuda \
		-o encode.o

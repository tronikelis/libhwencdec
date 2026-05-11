.PHONY: main
main: main.o
	./main.o

main.o: main.c
	clang \
		main.c \
		-I/opt/cuda/include \
		-lnvidia-encode \
		-lcuda \
		-o main.o

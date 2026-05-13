.PHONY: run_test
run_test: run_test.o
	./run_test.o

run_test.o: run_test.c encode.h decode.h
	clang \
		run_test.c \
		-I/opt/cuda/include \
		-lnvidia-encode \
		-lcuda \
		-lnvcuvid \
		-o run_test.o


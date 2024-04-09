VPATH=src:test
object=test.o leptjson.o
outpath=./out

# 使用 CFLAGS 控制 Makefile 自动推导标志
CFLAGS=-g -std=c89
CC=gcc

all : $(object)
	gcc $(CFLAGS) $(object) -o $(outpath)/test_out
	mv ./*.o $(outpath)

test.o:leptjson.h
leptjson.o:leptjson.h


.PHONY : clean
clean :
	rm -rf out/*
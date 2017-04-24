CC=gcc
#OPT=-g -fcilkplus
#LIBS=-lcilkrts -lpthread -ldl
#DEPS=parab.h Makefile
#OBJS=parab4.o
#TARGET=parab


#%.o: %.c $(DEPS)
#	$(CC) -o $@ $< $(OPT) 

#$(TARGET): $(OBJS)
#	$(CC) $(LIBS)  -o $@ $<


parab: parab4.c parab.h Makefile
	$(CC) -o parab -g -fcilkplus -lcilkrts -lpthread -ldl parab4.c

#time CILK_WORKERS=16 ./parab


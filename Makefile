CC=gcc
OPT=-g -fcilkplus
LIBS=-lcilkrts -lpthread -ldl
DEPS=parab.h Makefile
SRCS=parab8.c parab_util.c
#OBJS=parab4.o
TARGET=parab


#%.o: %.c $(DEPS)
#	$(CC) -o $@ $< $(OPT) 

#$(TARGET): $(OBJS)
#	$(CC) $(LIBS)  -o $@ $<


$(TARGET): $(SRCS) $(DEPS)
	$(CC) -o $(TARGET) $(OPT) $(LIBS) $(SRCS)

#time CILK_WORKERS=16 ./parab


all:
	gcc -o parab -g -fcilkplus -lcilkrts -lpthread -ldl parab3.c

#time CILK_WORKERS=16 ./parab

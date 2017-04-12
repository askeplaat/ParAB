all:
	gcc -o parab -fcilkplus -lcilkrts -lpthread -ldl parab.c

#time CILK_WORKERS=16 ./parab

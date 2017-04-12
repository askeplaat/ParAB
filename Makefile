all:
	gcc -o parab -fcilkplus -lcilkrts -lpthread -ldl parab2.c

#time CILK_WORKERS=16 ./parab

ALL: ricartAgra

ricartAgra: ricart_agrawala.c
	mpicc -Wall ricart_agrawala.c -o ricartAgra

run:
	mpirun -np 6 ricartAgra

clean:
	/bin/rm -f ricartAgra *.o


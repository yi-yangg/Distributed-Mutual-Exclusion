ALL: ricartAgra lodhaKshem

ricartAgra: ricart_agrawala.c
	mpicc -Wall ricart_agrawala.c -o ricartAgra

lodhaKshem: lodha_kshemkalyani.c
	mpicc -Wall lodha_kshemkalyani.c -o lodhaKshem

runRicart:
	mpirun -np 6 ricartAgra

runLodha:
	mpirun -np 6 lodhaKshem

run: runRicart runLodha

clean:
	/bin/rm -f ricartAgra *.o
	/bin/rm -f lodhaKshem *.o


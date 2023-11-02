#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <unistd.h>

#define N 4

// Initialize local state for process
int My_Sequence_Number = 0
bool RV[N];

typedef struct Request {
    int sequence_number;
    int process_id;
} Request;

Request* LRQ;
int Highest_Sequence_Number_Seen = 0;

// Function prototypes

bool CheckExecuteCs();




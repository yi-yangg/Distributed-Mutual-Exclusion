#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <time.h>

#define N 4 // no of processes
#define REQUEST 0
#define REPLY 1

#define TIMEOUT 5 // 5 seconds timeout

// Initialize local state for process Pi
int size, rank;
int My_Sequence_Number = 0;
int ReplyCount = 0;
bool RD[N];
int Highest_Sequence_Number_Seen = 0;


void requestCs();
void releaseCs();
void resetRD();
void listenIncomingRequest();

int main(int argc, char* argv[]) {
    time_t start_time, current_time;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int csInterest = rank % 2; // odd rank has interest in entering CS
    resetRD();

    // 5 iterations
    for (int i = 0; i < 5; i++) {
        if (csInterest) {

            // request critical section from all other processes
            requestCs();
            printf("Process %d is in the critical section", rank);

            // simulate work inside critical section
            time(&start_time);
            while(1) {
                time(&current_time);

                double elapsed_time = difftime(current_time, start_time);
                if (elapsed_time >= TIMEOUT) {
                    break;
                }
            }

            releaseCs();


        }
    }

}

// Function to invoke mutual exclusion and receive reply from other process
void requestCs() {
    My_Sequence_Number = Highest_Sequence_Number_Seen + 1;
    
    // Make REQUEST message Ri = {SN, i}
    int request_msg[2] = {My_Sequence_Number, rank};
    printf("Process %d request for critical section\n", rank);
    // Send REQUEST message to all other processes
    for (int j = 0; j < N; j++) {
        if (j != rank)
            MPI_Send(request_msg, 2, MPI_INT, j, REQUEST, MPI_COMM_WORLD);
    }

    ReplyCount = 0;
    resetRD();

    MPI_Request req[N-1];
    int req_count = 0;
    int reply_msg;
    // Recv REPLY from all other 
    for (int j = 0; j < N; j++) {
        if (j != rank)
            MPI_Irecv(&reply_msg, 1, MPI_INT, j, REPLY, MPI_WORLD_COMM, &req[req_count++]);
    }
    
    int reply_flag = 0;
    while (1) {
        MPI_Testall(N-1, req, &reply_flag, MPI_STATUSES_IGNORE);

        if (reply_flag) {
            break;
        }
    }
}

void releaseCs() {

}

void resetRD() {
    for (int j = 0; j < N; j++) {
        RD[j] = false;
    }
}

void listenIncomingRequest() {
    
}
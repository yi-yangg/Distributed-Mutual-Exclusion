#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <time.h>

#define N 4 // no of processes
#define REQUEST 0
#define REPLY 1

#define TIMEOUT 5 // 5 seconds timeout

// Initialize local state for process Pi
int size, rank, iter_count;
int My_Sequence_Number = 0;
int ReplyCount = 0;
bool RD[N];
int Highest_Sequence_Number_Seen = 0;
int request_flag = 0;
MPI_Request request_req;

void requestCs();
void releaseCs();
void resetRD();
void listenIncomingRequest(bool isRequesting);
void max();

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
            printf("-----------------------------------------------------------------\n");
            printf("Process %d is in the critical section", rank);

            // simulate work inside critical section
            time(&start_time);
            while(true) {
                time(&current_time);

                double elapsed_time = difftime(current_time, start_time);
                if (elapsed_time >= TIMEOUT) {
                    break;
                }
            }

            printf("Process %d exiting critical section", rank);
            printf("-----------------------------------------------------------------\n");
            releaseCs();
        }
        else {
            iter_count = 0;
            int max_iter = N / 2; // max number of odd rank processes
            while (true) {
                listenIncomingRequest(false);
                
            }
        }
        
    }

    MPI_Finalize();
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
    while (true) {
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

void listenIncomingRequest(bool isRequesting) {
    int recv_req[2];
    if (!request_flag) {
        MPI_Irecv(&recv_req, 2, MPI_INT, MPI_ANY_SOURCE, REQUEST, MPI_WORLD_COMM, &recv_req);
    }

    MPI_Status status;

    MPI_Test(&recv_req, &request_flag, &status);
    int Pj_SN = recv_req[0];
    int Pj_rank = recv_req[1];

    

    // if the process currently requesting
    // check priority between Pi and Pj
    if (request_flag) {
        int send_reply = isRequesting;
        if(isRequesting) {
            // if Pi higher priority than Pj
            if (My_Sequence_Number < Pj_SN || (My_Sequence_Number == Pj_SN && rank < Pj_rank)) {
                RD[Pj_rank] = true;
                Highest_Sequence_Number_Seen = Pj_SN < Highest_Sequence_Number_Seen ? Highest_Sequence_Number_Seen : Pj_SN;
            }

            else {
                send_reply = 0;
            }
        }

        if (!send_reply) {
            printf("Process %d sending REPLY to Process %d\n", rank, status.MPI_SOURCE);
            int temp_reply = 0;
            MPI_Send(&temp_reply, 1, MPI_INT, status.MPI_SOURCE, REPLY, MPI_COMM_WORLD);
            request_flag = 0; // reset request_flag to setup another async listener
            iter_count++; // only useful for even processes (no interest in cs)
        }
    }
    

}
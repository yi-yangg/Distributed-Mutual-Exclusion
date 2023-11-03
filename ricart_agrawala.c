#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <unistd.h>

#define N 6 // no of processes
#define REQUEST 0
#define REPLY 1
#define NONE -1

// Initialize local state for process Pi
int size, rank, iter_count;
int My_Sequence_Number = 0;
int ReplyCount = 0;
bool RD[N];
int request_msg[2];
int Highest_Sequence_Number_Seen = 0;

// Listener variables
int request_flag = NONE;
MPI_Request request_req;

void requestCs();
void releaseCs();
void resetRD();
void listenIncomingRequest(bool isRequesting);
void sendReply(int reply, int dest);

int main(int argc, char* argv[]) {
    // Initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int csInterest = rank % 2; // odd rank has interest in entering CS
    resetRD();

    // Main loop
    for (int i = 0; i < 1; i++) {
        if (csInterest) {
            // request critical section from all other processes
            requestCs();
            printf("-----------------------------------------------------------------\n");
            printf("Process %d is in the critical section\n", rank);
            // simulate work inside critical section
            sleep(1);

            printf("Process %d exiting critical section\n", rank);
            printf("-----------------------------------------------------------------\n");
            fflush(stdout);
            // release critical section and send REPLY to all deferred processes
            releaseCs();
        }
        else {
            iter_count = 0;
            int max_iter = N / 2; // max number of odd rank processes
            while (iter_count < max_iter) {
                listenIncomingRequest(false);
            }
        }

        
    }
    MPI_Finalize();

    return 0;
}

// Function to invoke mutual exclusion and receive reply from other process
void requestCs() {
    My_Sequence_Number = Highest_Sequence_Number_Seen + 1;
    
    // Make REQUEST message Ri = {SN, i}
    request_msg[0] = My_Sequence_Number;
    request_msg[1] = rank;
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
    // Recv REPLY from all other processes
    for (int j = 0; j < N; j++) {
        if (j != rank)
            MPI_Irecv(&reply_msg, 1, MPI_INT, j, REPLY, MPI_COMM_WORLD, &req[req_count++]);
    }
    int reply_flag = 0;
    // Test all async recvs while listening to any REQUEST made by other nodes
    while (true) {
        listenIncomingRequest(true);
        MPI_Testall(N-1, req, &reply_flag, MPI_STATUSES_IGNORE);
        if (reply_flag) {
            break;
        }
    }
}

// Function to release critical section
void releaseCs() {
    // Loop through the RD (Deferred list) and send reply to all deferred processes
    for (int i = 0; i < N; i++) {
        if(RD[i] && i != rank) {
            sendReply(0, i);
        }
    }
}

// Function to reset deferred list
void resetRD() {
    for (int j = 0; j < N; j++) {
        RD[j] = false;
    }
}

// Function to listen to any incoming request by other processes
void listenIncomingRequest(bool isRequesting) {
    // If request_flag is NONE then setup a new async listener
    if (request_flag == NONE) {
        MPI_Irecv(request_msg, 2, MPI_INT, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &request_req);
    }

    // Test the async listener
    MPI_Test(&request_req, &request_flag, MPI_STATUS_IGNORE);

    // if REQUEST received
    // check priority between Pi and Pj
    if (request_flag) {
        int Pj_SN = request_msg[0];
        int Pj_rank = request_msg[1];
        int send_reply = isRequesting;
        if(isRequesting) {
            // if Pi higher priority than Pj
            if (My_Sequence_Number < Pj_SN || (My_Sequence_Number == Pj_SN && rank < Pj_rank)) {
                RD[Pj_rank] = true;
                Highest_Sequence_Number_Seen = Pj_SN < Highest_Sequence_Number_Seen ? Highest_Sequence_Number_Seen : Pj_SN;
            }
            // else invoke a send REPLY to the process
            else {
                send_reply = 0;
            }
        }

        if (!send_reply) {
            sendReply(0, Pj_rank);
            iter_count++; // only useful for even processes (no interest in cs)
        }
        request_flag = NONE; // reset request_flag to setup another async listener

    }
}

// Function to send a REPLY to the passed in dest
void sendReply(int reply, int dest) {
    printf("Process %d sending REPLY to Process %d\n", rank, dest);
    MPI_Send(&reply, 1, MPI_INT, dest, REPLY, MPI_COMM_WORLD);
}
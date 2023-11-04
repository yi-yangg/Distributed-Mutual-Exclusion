#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#define N 6
#define REQUEST 0
#define REPLY 1
#define FLUSH 2
#define NONE -1

// Struct definition for all REQUESTs
typedef struct Request {
    int sequence_number;
    int process_id;
} Request;

// Initialize local state for process
int size, rank, iter_count;
int My_Sequence_Number = 0;
bool RV[N];
bool deferredProcs[N];
Request* LRQ = NULL;
int LRQ_size;
int Highest_Sequence_Number_Seen = 0;

// Custom MPI Datatype for Request struct
Request request, received_request;
MPI_Datatype requestType;

// Listener variables
int request_flag = NONE;
MPI_Request request_req;

// Function prototypes
void requestCs();
void InvMutEx();
void RcvReq(Request req, bool isRequesting);
void RcvReplyOrFlush(Request req);
void FinCS();
bool checkRV();
bool checkLRQHead();
bool CheckExecuteCs();
void resetRV();
void freeLRQ();
void resetDP();
int getLRQCount(Request req);
void removeReqFromLRQ(Request req);
void insertIntoLRQ(Request req);
void listenIncomingRequest(bool isRequesting);

int main(int argc, char* argv[]) {
    // Initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int csInterest = rank % 2; // Set odd rank to have interest in Critical Section
    // Reset the data structures
    resetRV();
    resetDP();

    // Create MPI custom struct datatype for REQUEST
    MPI_Datatype type[2] = {MPI_INT, MPI_INT};
    int blocklen[2] = {1, 1};
    MPI_Aint disp[2];

    MPI_Get_address(&request.sequence_number, &disp[0]);
    MPI_Get_address(&request.process_id, &disp[1]);

    disp[1] = disp[1] - disp[0];
    disp[0] = 0;

    MPI_Type_create_struct(2, blocklen, disp, type, &requestType);
	MPI_Type_commit(&requestType);
    
    // Initialize the initial REQUEST to have sequence number of 0
    request.sequence_number = 0;
    request.process_id = rank;

    // Main loop
    for (int i = 0; i < 1; i++) {
        if (csInterest) {

            // Request critical section from all other processes
            requestCs();
            printf("-----------------------------------------------------------------\n");
            printf("Process %d is in the critical section\n", rank);
            // Simulate work inside critical section
            printf("Currently in critical section\n");
            sleep(1);

            printf("Process %d exiting critical section\n", rank);
            printf("-----------------------------------------------------------------\n");
            fflush(stdout);
            FinCS();
        }
        else { // Even rank processes (no interest in CS), only receive request and send REPLY
            iter_count = 0;
            int max_iter = N / 2; // Max number of odd rank processes
            while (iter_count < max_iter) {
                listenIncomingRequest(false);
            }
        }

        
    }
    MPI_Finalize();
    freeLRQ();

    return 0;
}

// Function to invoke mutual exclusion and receive response from other process
void requestCs() {
    // Call function to invoke mutual exclusion
    InvMutEx();

    MPI_Request req[N-1];
    MPI_Status stat[N-1];
    int flags[N - 1];
    int req_count = 0;
    Request response_req[N-1];
    // Receive response from other processes
    for (int j = 0; j < N; j++) {
        if (j != rank) {
            MPI_Irecv(&response_req[req_count], 1, requestType, j, MPI_ANY_TAG, MPI_COMM_WORLD, &req[req_count]);
            req_count++;
        }
            
    }

    // For each of the processes, test the async receive while listening for incoming requests
    for (int i = 0; i < req_count; i++) {
        // Loop infinitely until async recieve receives a response
        while (true) {
            MPI_Test(&req[i], &flags[i], &stat[i]);
            listenIncomingRequest(true);
            if (flags[i]) {
                break;
            }
        }

        // Check the tag of the response (REQUEST, REPLY or FLUSH) and call corresponding functions
        if (stat[i].MPI_TAG == REQUEST) {
            RcvReq(response_req[i], true);
        }
        else {
            RcvReplyOrFlush(response_req[i]);
        }
    }

    // If not current process's turn then wait to receive a FLUSH response
    if (LRQ[0].process_id != rank) {
        Request flush_response;
        MPI_Recv(&flush_response, 1, requestType, MPI_ANY_SOURCE, FLUSH, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Received flush from %d\n", flush_response.process_id);
        RcvReplyOrFlush(flush_response);
    }
    

}

// Function to invoke mutual exclusion
void InvMutEx() {
    // Initialize new SN
    My_Sequence_Number = Highest_Sequence_Number_Seen + 1;
    // LRQ = NULL
    freeLRQ();

    // Make REQUEST(Ri)
    request.sequence_number = My_Sequence_Number;
    request.process_id = rank;
    LRQ_size = 1;
    // Insert REQUET in LRQ
    LRQ = (Request*) malloc(LRQ_size * sizeof(Request));
    LRQ[0] = request;

    // Send REQUEST to other processes
    for (int i = 0; i < N; i++) {
        if (i != rank) 
            MPI_Send(&request, 1, requestType, i, REQUEST, MPI_COMM_WORLD);
    }

    // Reset RV and set own RV to 1
    resetRV();
    RV[rank] = 1;
}

// Function to be called when a REQUEST is received
void RcvReq(Request req, bool isRequesting) {
    int SN = req.sequence_number;
    int sender = req.process_id;
    // Initialize new Highest SN = max(SN, Highest SN)
    Highest_Sequence_Number_Seen = SN < Highest_Sequence_Number_Seen ? Highest_Sequence_Number_Seen : SN;

    // If current process is requesting and sender is also requesting
    if(isRequesting) {
        // If sender hasn't replied before
        if (!RV[sender]) {
            // Insert Request to LRQ
            insertIntoLRQ(req);
            RV[sender] = true;
        }
        else { // If sender replied before, not concurrent
            // Defer the REQUEST
            deferredProcs[sender] = true;
        }
    }
    else {
        // If current process not REQUESTing then send REPLY to sender
        MPI_Send(&request, 1, requestType, sender, REPLY, MPI_COMM_WORLD);
    }
}

// Function to be called when a REPLY and FLUSH is received
void RcvReplyOrFlush(Request req) {
    int sender = req.process_id;
    // Set RV at sender index to indicate received REPLY
    RV[sender] = true;
    // Remove request that have higher priority than sender
    removeReqFromLRQ(req);

}

// Function when process finishes executing Critical Section (CS)
void FinCS() {
    // Remove all request that have higher priority
    removeReqFromLRQ(request);

    // If there are more REQUESTs in LRQ then send flush to next REQUEST
    if (LRQ_size > 0) {
        Request nextReq = LRQ[0];
        printf("Sending flush to %d\n", nextReq.process_id);
        MPI_Send(&request, 1, requestType, nextReq.process_id, FLUSH, MPI_COMM_WORLD);
    }
    
    // Send REPLY to all deferred processes
    for (int i = 0; i < N; i++) {
        if (deferredProcs[i]) {
            MPI_Send(&request, 1, requestType, i, REPLY, MPI_COMM_WORLD);
        }
    }
}

// Function to check if all processes has replied (all 1's)
bool checkRV() {
    for (int i = 0; i < N; i++) {
        if (!RV[i]) {
            return false;
        }
    }

    return true;
}

// Function to check if the head of LRQ belongs to the process
bool checkLRQHead() {
    return LRQ[0].process_id == rank;
}

// Function to check all process replies and check the head of LRQ
bool CheckExecuteCs() {
    return checkRV() && checkLRQHead();
}

// Function to reset the process reply list
void resetRV() {
    for (int j = 0; j < N; j++) {
        RV[j] = false;
    } 
}

// Function to free LRQ and set to NULL (prevent memory leak and dangling pointer)
void freeLRQ() {
    if (LRQ != NULL) {
        free(LRQ);
        LRQ = NULL;
    }
}

// Function to reset the deferred process list
void resetDP() {
    for (int j = 0; j < N; j++) {
        deferredProcs[j] = false;
    }
}

// Function that get the number of REQUESTs in LRQ that has higher priority than passed in REQUEST
int getLRQCount(Request req) {
    int SN = req.sequence_number;
    int sender = req.process_id;
    int count = 0;
    for(int i = 0; i < LRQ_size; i++) {
        if (LRQ[i].sequence_number > SN || (LRQ[i].sequence_number == SN && LRQ[i].process_id > sender)) {
            break;
        }

        else {
            count++;
        }
    }
    return count;
}

// Function to remove all REQUESTs that has higher priority (>=) to the passed in REQUEST
void removeReqFromLRQ(Request req) {
    int count = getLRQCount(req);
    
    for(int i = count; i < LRQ_size; i++) {
        LRQ[i - count] = LRQ[i];
    }

    LRQ_size -= count;
}

// Function to insert REQUEST into LRQ in sorted order
void insertIntoLRQ(Request req) {
    int count = getLRQCount(req);
    LRQ_size++;
    // Realloc memory based on current LRQ size
    LRQ = realloc(LRQ, LRQ_size * sizeof(Request));
    // Move all the lower priority back
    for(int i = LRQ_size - 1; i > count; i--) {
        LRQ[i] = LRQ[i - 1];
    }
    // Insert request at specified location
    LRQ[count] = req;

}

// Function to listen to any REQUEST by other processes
void listenIncomingRequest(bool isRequesting) {
    // Setup new async receive if request_flag is NONE
    if (request_flag == NONE) {
        MPI_Irecv(&received_request, 1, requestType, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &request_req);
    }

    // Check the async receive if it has received a response
    MPI_Test(&request_req, &request_flag, MPI_STATUS_IGNORE);

    // If REQUEST received
    if (request_flag) {
        // Call RcvReq() to process REQUEST
        RcvReq(received_request, isRequesting);

        request_flag = NONE; // reset request_flag to setup another async listener
        iter_count++; // only useful for even processes (no interest in cs)
    }
}
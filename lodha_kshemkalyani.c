#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#define N 4
#define REQUEST 0
#define REPLY 1
#define FLUSH 2
#define NONE -1

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

int request_flag = NONE;
MPI_Request request_req;
// Function prototypes
void requestCs();
void InvMutEx();
void RcvReq(Request req, bool isRequesting);
void RcvReplyOrFlush(Request req);
void FinCS();
bool CheckExecuteCs();
void resetRV();
void freeLRQ();
void resetDP();
int getLRQCount(Request req);
void removeReqFromLRQ(Request req);
void insertIntoLRQ(Request req);
void listenIncomingRequest(bool isRequesting);

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int csInterest = rank % 2;
    resetRV();

    MPI_Datatype type[2] = {MPI_INT, MPI_INT};
    int blocklen[2] = {1, 1};
    MPI_Aint disp[2];

    MPI_Get_address(&request.sequence_number, &disp[0]);
    MPI_Get_address(&request.process_id, &disp[1]);

    disp[1] = disp[1] - disp[0];
    disp[0] = 0;

    MPI_Type_create_struct(2, blocklen, disp, type, &requestType);
	MPI_Type_commit(&requestType);
    
    request.sequence_number = 0;
    request.process_id = rank;

    for (int i = 0; i < 1; i++) {
        if (csInterest) {

            // request critical section from all other processes
            requestCs();
            printf("-----------------------------------------------------------------\n");
            printf("Process %d is in the critical section\n", rank);
            // simulate work inside critical section
            printf("Currently in critical section\n");
            sleep(1);

            printf("Process %d exiting critical section\n", rank);
            printf("-----------------------------------------------------------------\n");
            fflush(stdout);
            FinCS();
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

void requestCs() {
    InvMutEx();

    MPI_Request req[N-1];
    MPI_Status stat[N-1];
    int flags[N - 1];
    int req_count = 0;
    Request response_req[N-1];
    // Recv response from other processes
    for (int j = 0; j < N; j++) {
        if (j != rank) {
            MPI_Irecv(&response_req[req_count], 1, requestType, j, MPI_ANY_TAG, MPI_COMM_WORLD, &req[req_count]);
            req_count++;
        }
            
    }


    for (int i = 0; i < req_count; i++) {
        while (true) {
            MPI_Test(&req[i], &flags[i], &stat[i]);
            listenIncomingRequest(true);
            if (flags[i]) {
                break;
            }
        }

        if (stat[i].MPI_TAG == REQUEST) {
            RcvReq(response_req[i], true);
        }
        else {
            RcvReplyOrFlush(response_req[i]);
        }
        int recv_rank = response_req[i].process_id;
        if (CheckExecuteCs()) {
            break;
        }
        else if (LRQ[0].process_id != rank) {
            MPI_Recv(&response_req[i], 1, requestType, recv_rank, FLUSH, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            printf("Received flush from %d\n", recv_rank);
            RcvReplyOrFlush(response_req[i]);
        }
    }
    

}

void InvMutEx() {
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


void RcvReq(Request req, bool isRequesting) {
    int SN = req.sequence_number;
    int sender = req.process_id;
    Highest_Sequence_Number_Seen = SN < Highest_Sequence_Number_Seen ? Highest_Sequence_Number_Seen : SN;

    if(isRequesting) {
        if (!RV[sender]) {
            insertIntoLRQ(req);
            RV[sender] = true;
        }
        else {
            deferredProcs[sender] = true;
        }
    }
    else {
        MPI_Send(&request, 1, requestType, sender, REPLY, MPI_COMM_WORLD);
    }
}

void RcvReplyOrFlush(Request req) {
    int sender = req.process_id;

    RV[sender] = true;
    removeReqFromLRQ(req);

}

void FinCS() {
    removeReqFromLRQ(request);
    if (LRQ_size > 0) {
        Request nextReq = LRQ[0];
        printf("Sending flush to %d\n", nextReq.process_id);
        MPI_Send(&request, 1, requestType, nextReq.process_id, FLUSH, MPI_COMM_WORLD);
    }
    
    for (int i = 0; i < N; i++) {
        if (deferredProcs[i]) {
            MPI_Send(&request, 1, requestType, i, REPLY, MPI_COMM_WORLD);
        }
    }
}
bool CheckExecuteCs() {
    for (int i = 0; i < N; i++) {
        if (!RV[i]) {
            return false;
        }
    }
    return LRQ[0].process_id == rank;
}

void resetRV() {
    for (int j = 0; j < N; j++) {
        RV[j] = false;
    } 
}

void freeLRQ() {
    if (LRQ != NULL) {
        free(LRQ);
        LRQ = NULL;
    }
}

void resetDP() {
    for (int j = 0; j < N; j++) {
        deferredProcs[j] = false;
    }
}

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

void removeReqFromLRQ(Request req) {
    int count = getLRQCount(req);

    for(int i = count; i < LRQ_size; i++) {
        LRQ[i - count] = LRQ[i];
    }

    LRQ_size -= count;
}

void insertIntoLRQ(Request req) {
    int count = getLRQCount(req);
    LRQ_size++;
    LRQ = realloc(LRQ, LRQ_size * sizeof(Request));
    for(int i = LRQ_size - 1; i > count; i--) {
        LRQ[i] = LRQ[i - 1];
    }

    LRQ[count] = req;

}


void listenIncomingRequest(bool isRequesting) {
    
    if (request_flag == NONE) {
        MPI_Irecv(&received_request, 1, requestType, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &request_req);
    }

    MPI_Test(&request_req, &request_flag, MPI_STATUS_IGNORE);

    // if the process currently requesting
    if (request_flag) {
        RcvReq(received_request, isRequesting);

        request_flag = NONE; // reset request_flag to setup another async listener
        iter_count++;
    }
}
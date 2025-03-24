#include "mpi.h"
#include <cmath>
#include <vector>
#include "Communication_Hash.h"
#include "UTS_Strategy.h"

void printOutPut(long count, double t1, double t2) {
    printf("Generated %ld nodes\n", count);
    printf("Elapsed time: %f\n", t2 - t1);
}

bool parseParameters(int argc ,char* argv[],int& d ,int& startingProcesses, int& maxProcesses, int& evolving, int& seconds) {
    if(3 > argc || argc > 6) {
        printf("Usage: <depth> <evolving> <seconds> <startingProcesses> <maxProcesses/newProcesses>\n");
        return true;
    }
    d = atoi(argv[1]);
    evolving = atoi(argv[2]);

    if(argc == 6) {
        seconds = atoi(argv[3]);
        startingProcesses = atoi(argv[4]);
        maxProcesses = atoi(argv[5]);
    }
    return false;
}


void checkGeneratedCounts(int depth, long count) {
    if(depth > 18) {
        printf("Only checkable up to a depth of 18.\n");
        return;
    }

    long results[] = {
        0L,
        6L,
        65L,
        254L,
        944L,
        3987L,
        16000L,
        63914L,
        257042L,
        1031269L,
        4130071L,
        16526523L,
        66106929L,
        264459392L,
        1057675516L,
        4230646601L,
        16922208327L,
        67688164184L,
        270751679750L
    };

    if(results[depth] == count) {
        printf("Generated nodes are correct\n");
    } else {
        printf("Generated nodes are not correct\n");
    }
}

int main(int argc, char* argv[]) {
    int startingProcesses = 0;
    int maxProcesses = 0;
    int evolving = 0;
    int seconds = 0;
    int d = 0;

    if(parseParameters(argc, argv, d, startingProcesses, maxProcesses, evolving, seconds)) {
        return 0;
    }

    double t1,t2;
    MPI_Session session = MPI_SESSION_NULL;

    int seed = 19;
    int b = 4;
    double den = log(b / (1.0 + b));

    UTS_Strategy strategy = UTS_Strategy(64, den);

    t1 = MPI_Wtime();

    Communication_Hash communication = Communication_Hash(session, startingProcesses, maxProcesses, evolving, seconds);

    if(!communication.initDynamicResourceChange()) {
        return 0;
    }

    if(communication.rank == 0) {
        strategy.seed(seed, d);
    }

    int wakeUPCalls = 0;
    int recVData = 0;
    int requestedForData = 0;
    double workingTime = 0;
    double timeStampTmp;

    MPI_Comm& comm = communication.getComm();
    int& rank = communication.getRank();
    int& size = communication.getSize();
    int& terminationFlag = communication.getTerminationFlag();
    bool& dynamic_flag = communication.getDynamicProcessFlag();

    if(rank == 0) {
        printf("UTS_MPI_Stealing_Hash starts with seed=%d, depth=%d, branching factor=%d\n", seed, d, b);
    }

    int done = 0;

    std::vector<int>& lifeLinePartner = communication.getLifeLinePartner();

    timeStampTmp = MPI_Wtime();
    strategy.process(communication);
    workingTime += MPI_Wtime() - timeStampTmp;

    while(!done && !communication.earlyTermination) {
        int requestAmount = lifeLinePartner.size();
        while(requestAmount && !communication.earlyTermination) {
            requestedForData++;
            communication.requestWork(0, communication.getRandomRequestPartner(), strategy);

            if(strategy.size > 0) {
                recVData++;
                timeStampTmp = MPI_Wtime();
                strategy.process(communication);
                workingTime += MPI_Wtime() - timeStampTmp;
                requestAmount = lifeLinePartner.size();
            }
            requestAmount--;
        }

        if(!terminationFlag) {
            if(communication.sendIdle(strategy)) {
                continue;
            }
            communication.rebuildWakeUp();
            communication.sendIdleViaLifeLine();
        }

        while(!communication.earlyTermination) {
            if(rank != 0) {
                if(communication.checkResourceChangeLocal(strategy) && communication.cancelIdle(strategy)) {
                    break;
                }
            } else {
                if(communication.checkTime(strategy) && communication.cancelIdle(strategy)) {
                    break;
                }
            }

            if(communication.checkWakeUp() && communication.cancelIdle(strategy)) {
                wakeUPCalls++;
                break;
            }

            communication.checkForRequest(strategy);

            if(rank == 0) {
                if(communication.lookForTermination()) {
                    for(int i = 1; i < size; i++) {
                        communication.sendTerminate(i);
                    }
                    done = 1;
                    break;
                }
            }

            if(terminationFlag) {
                done = 1;
                break;
            }
        }
    }
    communication.cancelAllListener();
    long global_count = 0;
    long counts = strategy.getResult();
    if(!communication.earlyTermination) {
        MPI_Reduce(&counts, &global_count, 1, MPI_LONG, MPI_SUM, 0, comm);
    }

    if(rank == 0){
        t2 = MPI_Wtime();
        printOutPut(global_count, t1, t2);
        checkGeneratedCounts(d, global_count);
    }

    if(!communication.earlyTermination) {
        communication.printOutTime();
    }

    communication.beforeFinalize = communication.beforeFinalize - MPI_Wtime();

    MPI_Comm_disconnect(&comm);
    MPI_Session_finalize(&session);

    return 0;
}
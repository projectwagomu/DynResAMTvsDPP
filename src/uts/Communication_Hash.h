#include "mpi.h"
#include <vector>

class UTS_Strategy;

class Communication_Hash {
public:
    int rank;
    long count;
    bool earlyTermination;
    int sending;
    double initTime;
    double psetQueryTime;
    double resourceChangeTime;
    double timeToFinish;
    double beforeFinalize;

    Communication_Hash(MPI_Session& session, int startingProcesses, int maxProcesses, int evolving, int seconds);
    ~Communication_Hash();
    void init();
    std::vector<int>& getLifeLinePartner();
    void responseWork(int flag, int rank);
    void rebuildWakeUp();
    void cancelAllListener();
    bool lookForTermination();
    void buildListener(int* &list, std::vector<MPI_Request> &requests, int tag);
    void reBuildLostRequest(int* &flagList, std::vector<MPI_Request> &requests, int to, int tag, int index);
    bool sendIdle(UTS_Strategy& nodes);
    bool cancelIdle(UTS_Strategy& nodes);
    void receiveIdle();
    void sendTerminate(int to);
    void recvTerminate();
    void cancelAllRequests(std::vector<MPI_Request>& requests);
    void sendIdleViaLifeLine();
    void sendWakeUp(int to);
    bool checkWakeUp();

    void requestWork(int flag, int from, UTS_Strategy& nodes);
    bool checkForRequest(UTS_Strategy& nodes);
    bool checkForResponse(UTS_Strategy& nodes);
    void sendingWork(UTS_Strategy& nodes, int to);
    void checkIdleViaLifeLine();

    void defineFlagList(int *&flagList, int size);
    void buildLifeLinePartner();

    void broadCastListenerForResourceChange();
    void notifyResourceChange();

    bool checkResourceChangeLocal(UTS_Strategy& nodes);
    bool initDynamicResourceChange();
    bool checkTime(UTS_Strategy& nodes);

    int getRandomRequestPartner();
    void printOutTime();

    MPI_Comm& getComm();
    int& getRank();
    int& getSize();
    int& getTerminationFlag();
    bool& getDynamicProcessFlag();

private:
    int* flagListRequests;
    int* flagListResponses;
    int* idleListenerFlag;
    int* wakeUpFlag;

    MPI_Session session;
    MPI_Comm comm;
    char main_pset[MPI_MAX_PSET_NAME_LEN];
    char delta_pset[MPI_MAX_PSET_NAME_LEN];
    char *dict_key;

    int op;

    MPI_Request resourceChangeRequest;
    MPI_Request terminationRequest;

    int broadCastBuffer;
    int terminationFlag;

    std::vector<MPI_Request> requestsForRequest;

    std::vector<MPI_Request> requestsForResponse;

    std::vector<MPI_Request> requestsForStatus;

    std::vector<MPI_Request> idleListener;

    std::vector<MPI_Request> requestsForWakeUp;

    std::vector<int> lifeLinePartner;
    std::vector<int> lifeLineFlag;
    std::vector<int> status;
    std::vector<int> bufferedStatus;

    int startingProcesses;
    int maxProcesses;

    int dimension;
    int size;
    bool primaryProcess;
    bool dynamic_process;

    int evolving;
    int seconds;

    double timeStarted;
    double t2;

    bool gettingNewPset(UTS_Strategy& nodes);
    int calculateProcNumber(int op);
    void receivingWork(UTS_Strategy &nodes, int from, int tag);
    void free_string_array(char **array, int size);
    void sendingAllRemainingWork(UTS_Strategy& nodes, int possibleRanks);
    void triggerResourceChange(UTS_Strategy& nodes);
    char* itoa(int num);
    void comm_create_from_group();
    void sendCounts(long count);
    void sendingToZero(UTS_Strategy& nodes);
    void recvCounts(int from, int to, UTS_Strategy& nodes);

};

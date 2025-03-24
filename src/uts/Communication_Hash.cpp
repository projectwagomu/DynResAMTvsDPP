#include "Communication_Hash.h"
#include <cmath>
#include <cstring>
#include <random>
#include "UTS_Strategy.h"

static std::random_device rd;
static std::mt19937 gen(rd());

static std::uniform_int_distribution<> growShrink(0, 1);

Communication_Hash::Communication_Hash(MPI_Session& session, int startingProcesses, int maxProcesses, int evolving, int seconds) {
    initTime = MPI_Wtime();
    MPI_Session_init(MPI_INFO_NULL, MPI_ERRORS_ARE_FATAL, &this->session);
    initTime = MPI_Wtime() - initTime;
    session = this->session;
    this->startingProcesses = startingProcesses;
    this->maxProcesses = maxProcesses;
    this->evolving = evolving;
    this->seconds = seconds;
    strcpy(delta_pset, "");
    strcpy(main_pset, "mpi://WORLD");
    dict_key = strdup("main_pset");
    timeStarted = MPI_Wtime();
    earlyTermination = false;
    count = 0;
    terminationFlag = 0;
    sending = 0;
    psetQueryTime = 0;
}

Communication_Hash::~Communication_Hash() {
    if(earlyTermination) {
        printf("Rank: %d is going with %f\n", rank, MPI_Wtime() - timeToFinish);
        printf("Rank: %d before finalize %f\n", rank, beforeFinalize);
    }
}
void Communication_Hash::init() {
    buildLifeLinePartner();

    lifeLineFlag.resize(lifeLinePartner.size(), 1);
    defineFlagList(flagListRequests, lifeLinePartner.size());
    defineFlagList(flagListResponses, lifeLinePartner.size());
    defineFlagList(idleListenerFlag, lifeLinePartner.size());
    defineFlagList(wakeUpFlag, lifeLinePartner.size());
    if(rank == 0) {
        status.clear();
        bufferedStatus.clear();
        status.resize(size, 0);
        bufferedStatus.resize(size, 0);
        receiveIdle();
    } else {
        recvTerminate();
        broadCastListenerForResourceChange();
    }
    buildListener(flagListRequests, requestsForRequest, 1);
    buildListener(flagListResponses, requestsForResponse, 2);
    buildListener(idleListenerFlag, idleListener, 7);
    buildListener(wakeUpFlag, requestsForWakeUp, 6);

    MPI_Barrier(comm);
    if(rank == 0) {
        printf("Current amount of working processes: %d\n", size);
    }
}

std::vector<int>& Communication_Hash::getLifeLinePartner() {
   return lifeLinePartner;
}

void Communication_Hash::rebuildWakeUp() {
    cancelAllRequests(requestsForWakeUp);
    buildListener(wakeUpFlag, requestsForWakeUp,6);
}

void Communication_Hash::cancelAllListener() {
    cancelAllRequests(requestsForRequest);
    cancelAllRequests(requestsForResponse);
    cancelAllRequests(requestsForWakeUp);
    cancelAllRequests(idleListener);
    delete[] flagListRequests;
    delete[] flagListResponses;
    delete[] idleListenerFlag;
    delete[] wakeUpFlag;
    if(rank == 0) {
        cancelAllRequests(requestsForStatus);
    } else {
        if(terminationRequest != MPI_REQUEST_NULL) {
            MPI_Cancel(&terminationRequest);
            MPI_Request_free(&terminationRequest);
        }
        if(resourceChangeRequest != MPI_REQUEST_NULL) {
            MPI_Cancel(&resourceChangeRequest);
            MPI_Request_free(&resourceChangeRequest);
        }
    }
}

void Communication_Hash::requestWork(int flag, int from, UTS_Strategy &nodes) {
    MPI_Send(&flag, 1, MPI_INT, from, 1, comm);
    if(flag == 0) {
        while(!checkForResponse(nodes) && !terminationFlag) {
            // wait for response
            checkForRequest(nodes);
            if(rank == 0) {
                lookForTermination();
                if(checkTime(nodes)) {
                    break;
                }
            } else {
                if(checkResourceChangeLocal(nodes)) {
                    break;
                }
            }
        }
    }
}

bool Communication_Hash::lookForTermination() {
    int count;
    int index[requestsForStatus.size()];

    MPI_Testsome(requestsForStatus.size(), requestsForStatus.data(), &count, index, MPI_STATUS_IGNORE);

    if(count > 0) {
        for(int i = 0; i < count; i++) {
            MPI_Irecv(&bufferedStatus[index[i] + 1], 1, MPI_INT, index[i] + 1, 3, comm, &requestsForStatus[index[i]]);
            status[index[i] + 1] = bufferedStatus[index[i] + 1];
            MPI_Send(&bufferedStatus[index[i] + 1], 1, MPI_INT, index[i] + 1, 3, comm);
        }
        return false;
    }

    if(status[0] == 2) {
        for (int i = 1; i < status.size(); i++) {
            if(status[i] != 2) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void Communication_Hash::buildListener(int* &list, std::vector<MPI_Request> &requests, int tag) {
    requests.clear();
    requests.resize(lifeLinePartner.size());

    for(int i = 0; i < lifeLinePartner.size(); i++) {
        int partner = lifeLinePartner[i];
        MPI_Irecv(&list[i], 1, MPI_INT, partner, tag, comm, &requests[i]);
    }
}

bool Communication_Hash::checkForRequest(UTS_Strategy& nodes) {
    int count;
    int index[requestsForRequest.size()];

    MPI_Testsome(requestsForRequest.size(), requestsForRequest.data(), &count, index, MPI_STATUS_IGNORE);

    if(count > 0) {
        for(int i = 0; i < count; i++) {
            int sendingRank = lifeLinePartner[index[i]];
            reBuildLostRequest(flagListRequests, requestsForRequest, sendingRank, 1, index[i]);

            if(flagListRequests[index[i]] == 0) {
                flagListRequests[index[i]] = -1;
                lifeLineFlag[index[i]] = nodes.checkIsSplittable();

                if(nodes.checkIsSplittable()) {
                    responseWork(1, sendingRank);
                    sendingWork(nodes, sendingRank);
                } else {
                    responseWork(0, sendingRank);
                }
            }
        }
        return true;
    }
    return false;
}

bool Communication_Hash::checkForResponse(UTS_Strategy &nodes) {
    int flag;
    int index;
    MPI_Testany(requestsForResponse.size(), requestsForResponse.data(), &index, &flag, MPI_STATUS_IGNORE);
    if (flag) {
        int sendingRank = lifeLinePartner[index];

        reBuildLostRequest(flagListResponses, requestsForResponse, sendingRank,2, index);

        if (flagListResponses[index] == 1) {
            lifeLineFlag[index] = 1;
            receivingWork(nodes, sendingRank, 0);
        }
        if(flagListResponses[index] == 0) {
            lifeLineFlag[index] = 0;
        }

        flagListResponses[index] = 0;
        return true;
    }
    return false;
}

void Communication_Hash::reBuildLostRequest(int* &flagList, std::vector<MPI_Request> &requests, int to, int tag, int index) {
    MPI_Request request;
    MPI_Irecv(&flagList[index], 1, MPI_INT, to, tag, comm, &request);
    requests[index] = request;
}

/**
 * true -> resource change
 * false -> no resource change
 * @param nodes
 */
bool Communication_Hash::sendIdle(UTS_Strategy& nodes) {
    if(rank == 0) {
        status[0] = 2;
        return false;
    }
    int idle = 2;
    MPI_Send(&idle, 1, MPI_INT, 0, 3, comm);
    MPI_Request statusRequest;
    MPI_Irecv(&idle, 1, MPI_INT, 0, 3, comm, &statusRequest);
    while(!checkResourceChangeLocal(nodes)) {
        int flag = 0;
        MPI_Test(&statusRequest, &flag, MPI_STATUS_IGNORE);
        if(flag) {
            return false;
        }
    }
    return true;
}

/**
 * true -> cancel worked || resource change
 * false -> terminate
 * @param nodes
 * @return
 */
bool Communication_Hash::cancelIdle(UTS_Strategy& nodes) {
    if(earlyTermination) {
        return true;
    } else if(rank == 0) {
        status[0] = 0;
        return true;
    }
    int idle = 0;
    MPI_Send(&idle, 1, MPI_INT, 0, 3, comm);
    MPI_Request statusRequest;
    MPI_Irecv(&idle, 1, MPI_INT, 0, 3, comm, &statusRequest);
    bool change = false;

    while(!terminationFlag && !(change = checkResourceChangeLocal(nodes))) {
        int flag = 0;
        MPI_Test(&statusRequest, &flag, MPI_STATUS_IGNORE);
        if(flag) {
            return true;
        }
    }
    MPI_Cancel(&statusRequest);
    MPI_Request_free(&statusRequest);
    return change;
}

void Communication_Hash::receiveIdle() {
    requestsForStatus.clear();
    requestsForStatus.resize(size - 1);
    for(int i = 1; i < size; i++) {
        MPI_Irecv(&bufferedStatus[i], 1, MPI_INT, i, 3, comm, &requestsForStatus[i - 1]);
    }
}

/**
 * flag = 0 -> dont have work to share
 * flag = 1 -> have work to share
 * @param flag
 * @param rank
 * @param comm
 */
void Communication_Hash::responseWork(int flag, int rank) {
    MPI_Send(&flag, 1, MPI_INT, rank, 2, comm);
}

void Communication_Hash::sendTerminate(int to) {
    int flag = 1;
    MPI_Send(&flag, 1, MPI_INT, to, 4, comm);
}
void Communication_Hash::recvTerminate() {
    MPI_Irecv(&terminationFlag, 1, MPI_INT, 0, 4, comm, &terminationRequest);
}

void Communication_Hash::cancelAllRequests(std::vector<MPI_Request> &requests) {
    for (auto &request: requests) {
        if(request != MPI_REQUEST_NULL) {
            MPI_Cancel(&request);
            MPI_Request_free(&request);
        }
    }
}

void Communication_Hash::sendingWork(UTS_Strategy& nodes, int to) {
    UTS_Strategy subUTS = nodes.split();

    MPI_Send(&subUTS.size , 1, MPI_INT, to, 0, comm);

    MPI_Send(subUTS.depth.data(), subUTS.size, MPI_INT, to, 0, comm);
    MPI_Send(subUTS.lower.data(), subUTS.size, MPI_INT, to, 0, comm);
    MPI_Send(subUTS.upper.data(), subUTS.size, MPI_INT, to, 0, comm);
    MPI_Send(subUTS.hash.data(), subUTS.size * 20, MPI_UNSIGNED_CHAR, to, 0, comm);
}

void Communication_Hash::receivingWork(UTS_Strategy &nodes, int from, int tag) {
    int size;
    MPI_Recv(&size, 1, MPI_INT, from, tag, comm, MPI_STATUS_IGNORE);
    if(size) {
        UTS_Strategy subUTS = UTS_Strategy(size, nodes.den);
        MPI_Recv(subUTS.depth.data(), size, MPI_INT, from, tag, comm, MPI_STATUS_IGNORE);
        MPI_Recv(subUTS.lower.data(), size, MPI_INT, from, tag, comm, MPI_STATUS_IGNORE);
        MPI_Recv(subUTS.upper.data(), size, MPI_INT, from, tag, comm, MPI_STATUS_IGNORE);
        MPI_Recv(subUTS.hash.data(), size * 20, MPI_UNSIGNED_CHAR, from, tag, comm, MPI_STATUS_IGNORE);

        subUTS.size = size;
        nodes.merge(subUTS);
    }
}

void Communication_Hash::sendWakeUp(int to) {
    int wakeUp = 1;
    MPI_Send(&wakeUp, 1, MPI_INT, to, 6, comm);
}

bool Communication_Hash::checkWakeUp() {
    int count;
    int index[requestsForWakeUp.size()];
    MPI_Testsome(requestsForWakeUp.size(), requestsForWakeUp.data(), &count, index, MPI_STATUS_IGNORE);
    if(count > 0) {
        for(int i = 0; i < count; i++) {
         MPI_Irecv(&wakeUpFlag[index[i]], 1, MPI_INT, lifeLinePartner[index[i]], 6, comm, &requestsForWakeUp[index[i]]);
        }
        return true;
    }
    return false;
}

void Communication_Hash::sendIdleViaLifeLine() {
    for(int i : lifeLinePartner) {
        int flag = 0;
        MPI_Send(&flag, 1, MPI_INT, i, 7, comm);
    }
}

void Communication_Hash::checkIdleViaLifeLine() {
    int index[idleListener.size()];
    int count;
    MPI_Testsome(idleListener.size(), idleListener.data(), &count, index, MPI_STATUS_IGNORE);
    if(count > 0) {
        for(int i = 0; i < count; i++) {
            sendWakeUp(lifeLinePartner[index[i]]);
            MPI_Irecv(&idleListenerFlag[index[i]], 1, MPI_INT, lifeLinePartner[index[i]], 7, comm, &idleListener[index[i]]);
        }
    }
}

void Communication_Hash::comm_create_from_group() {
    MPI_Group wgroup = MPI_GROUP_NULL;
    MPI_Group_from_session_pset (session, main_pset, &wgroup);
    MPI_Comm_create_from_group(wgroup, "mpi.forum.example", MPI_INFO_NULL, MPI_ERRORS_RETURN, &comm);

    if(MPI_COMM_NULL == comm){
        printf("comm_from_group returned MPI_COMM_NULL\n");
    }

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    dimension = ceil(std::log2(size));

    MPI_Group_free(&wgroup);
}

void Communication_Hash::defineFlagList(int* &flagList, int size) {
    flagList = new int[size];
    for(int i = 0; i < size; i++) {
        flagList[i] = -1;
    }
}

void Communication_Hash::buildLifeLinePartner() {
    this->lifeLinePartner.clear();
    for(int i = 0; i < this->dimension; i++) {
        int partner = this->rank ^ (1 << i);
        if(partner < this->size) {
            this->lifeLinePartner.push_back(partner);
        }
    }
}

int Communication_Hash::getRandomRequestPartner() {
    std::uniform_int_distribution<> dis(0, lifeLinePartner.size() - 1);
    return lifeLinePartner[dis(gen)];
}

MPI_Comm& Communication_Hash::getComm() { return comm; }
int& Communication_Hash::getRank() { return rank; }
int& Communication_Hash::getSize() { return size; }
int& Communication_Hash::getTerminationFlag() { return terminationFlag; }
bool& Communication_Hash::getDynamicProcessFlag() { return dynamic_process; }

bool Communication_Hash::checkTime(UTS_Strategy &nodes) {
    if(!evolving) {
        return false;
    }
    t2 = MPI_Wtime();
    if(evolving > 0 && t2 - timeStarted > seconds) {
        if(evolving == 1) {
            evolving = 0;
        }

        if(evolving == 2) {
            timeStarted = t2;
        }
        triggerResourceChange(nodes);
        return true;
    }
    return false;
}

void Communication_Hash::sendingAllRemainingWork(UTS_Strategy &nodes, int possibleRanks) {
    int sizeToShare = possibleRanks;
    int baseChunkSize = nodes.size / sizeToShare;
    int extra = nodes.size % sizeToShare;

    for (int i = 0; i < possibleRanks; i++) {
        int problemStart = i * baseChunkSize + std::min(i, extra);
        int problemEnd = (i + 1) * baseChunkSize + std::min(i + 1, extra);

        UTS_Strategy subNodes = nodes.splitSub(problemStart, problemEnd);
        MPI_Send(&subNodes.size, 1, MPI_INT, i, 10, comm);

        if(subNodes.size == 0) {
            continue;
        }
        MPI_Send(subNodes.depth.data(), subNodes.size, MPI_INT, i, 10, comm);
        MPI_Send(subNodes.lower.data(), subNodes.size, MPI_INT, i, 10, comm);
        MPI_Send(subNodes.upper.data(), subNodes.size, MPI_INT, i, 10, comm);
        MPI_Send(subNodes.hash.data(), subNodes.size * 20, MPI_UNSIGNED_CHAR, i, 10, comm);
    }
}

void Communication_Hash::sendingToZero(UTS_Strategy &nodes) {
    MPI_Send(&nodes.size, 1, MPI_INT, 0, 10, comm);
    if(nodes.size == 0) {
        return;
    }
    MPI_Send(nodes.depth.data(), nodes.size, MPI_INT, 0, 10, comm);
    MPI_Send(nodes.lower.data(), nodes.size, MPI_INT, 0, 10, comm);
    MPI_Send(nodes.upper.data(), nodes.size, MPI_INT, 0, 10, comm);
    MPI_Send(nodes.hash.data(), nodes.size * 20, MPI_UNSIGNED_CHAR, 0, 10, comm);
}

bool Communication_Hash::initDynamicResourceChange() {
    MPI_Info info;
    char boolean_string[16];
    int flag;

    MPI_Session_get_pset_info(session, main_pset, &info);

    MPI_Info_get(info, "mpi_dyn", 6, boolean_string, &flag);

    MPI_Info_free(&info);
    if((dynamic_process = (flag && 0 == strcmp(boolean_string, "True")))){
        MPI_Session_get_pset_data (session, main_pset, main_pset, (char **) &dict_key, 1, true, &info);
        MPI_Info_get(info, "main_pset", MPI_MAX_PSET_NAME_LEN, main_pset, &flag);

        if(!flag){
            printf("No 'next_main_pset' was provided for dynamic process. Terminate.\n");
            MPI_Session_finalize(&session);
            return false;
        }

        MPI_Info_free(&info);
        /* Get PSet data stored on main PSet */
        MPI_Session_get_pset_info (session, main_pset, &info);
    }

    comm_create_from_group();

    if(dynamic_process) {
        //printf("Dynamic Process started with rank %d\n", rank);
    }

    MPI_Session_get_pset_info(session, main_pset, &info);
    MPI_Info_get(info, "mpi_primary", 6, boolean_string, &flag);
    primaryProcess = (0 == strcmp(boolean_string, "True"));
    MPI_Info_free(&info);
    if(primaryProcess && dynamic_process) {
        MPI_Session_dyn_finalize_psetop(session, main_pset);
    }

    init();

    return true;
}

void Communication_Hash::broadCastListenerForResourceChange() {
    MPI_Irecv(&broadCastBuffer, 1, MPI_INT, 0, 11, comm, &resourceChangeRequest);
}

void Communication_Hash::notifyResourceChange() {
    int buffer = 1;
    for(int i = 1; i < size; i++) {
        MPI_Send(&buffer, 1, MPI_INT, i, 11, comm);
    }
}


bool Communication_Hash::checkResourceChangeLocal(UTS_Strategy &nodes) {
    int bufferFlag = 0;
    MPI_Test(&resourceChangeRequest, &bufferFlag, MPI_STATUS_IGNORE);
    if(bufferFlag > 0) {
        MPI_Bcast(&delta_pset, MPI_MAX_PSET_NAME_LEN, MPI_CHAR, 0, comm);
        MPI_Bcast(&op, 1, MPI_INT, 0, comm);
        terminationFlag = gettingNewPset(nodes);
        return true;
    }
    return false;
}

bool Communication_Hash::gettingNewPset(UTS_Strategy &nodes) {
    timeToFinish = MPI_Wtime();
    beforeFinalize = timeToFinish;
    MPI_Barrier(comm);
    if(rank == 0) {
        printf("Starting time for gettingNewPset: %f\n", MPI_Wtime());
    }
    checkForResponse(nodes);

    MPI_Info info;
    char boolean_string[16];
    int flag;

    bool terminate = false;
    /* Is proc included in the delta PSet? If no, need to terminate */
    MPI_Session_get_pset_info (session, delta_pset, &info);

    MPI_Info_get(info, "mpi_included", 6, boolean_string, &flag);
    if(0 == strcmp(boolean_string, "True")){
        terminate = true;
    }
    MPI_Info_get(info, "mpi_size", 6, boolean_string, &flag);
    int testSize = atoi(boolean_string);

    MPI_Info_free(&info);

    int possibleRanks = size - testSize;

    double tmp = MPI_Wtime();
    if(op == MPI_PSETOP_SHRINK) {
        if(terminate) {
            earlyTermination = true;
            sendingToZero(nodes);
            sendCounts(nodes.getResult());
            nodes.clear();
            return true;
        } else {
            //getting data
            if(rank == 0) {
                for(int partner = possibleRanks; partner < size; partner++) {
                    receivingWork(nodes, partner, 10);
                }
                recvCounts(possibleRanks, size, nodes);
            }
        }
    }
    if(rank == 0) {
        printf("Redistribution overall time: %f\n", MPI_Wtime() - tmp);
    }

    double newTmp = MPI_Wtime();
    MPI_Session_get_pset_data (session, main_pset, delta_pset, (char **) &dict_key, 1, true, &info);
    MPI_Info_get(info, "main_pset", MPI_MAX_PSET_NAME_LEN, main_pset, &flag);

    MPI_Info_free(&info);

    MPI_Session_get_pset_info (session, main_pset, &info);

    MPI_Info_get(info, "mpi_primary", 6, boolean_string, &flag);
    primaryProcess = (0 == strcmp(boolean_string, "True"));
    MPI_Info_free(&info);

    double cancelTime = MPI_Wtime();
    cancelAllListener();
    if(rank == 0) {
        printf("cancelAllListener time: %f\n", MPI_Wtime() - cancelTime);
    }

    MPI_Comm_disconnect(&comm);
    // build new lifelines and communicators;
    comm_create_from_group();
    if(primaryProcess){
        MPI_Session_dyn_finalize_psetop(session, main_pset);
    }

    if(rank == 0) {
        printf("After redistribution time: %f\n", MPI_Wtime() - newTmp);
    }

    initTime = 0;
    init();
    resourceChangeTime = MPI_Wtime() - resourceChangeTime;
    if(rank == 0) {
        printf("Resource Change time finished: %f\n", MPI_Wtime());
    }
    dynamic_process = false;
    return false;
}

void Communication_Hash::sendCounts(long count) {
    MPI_Send(&count, 1, MPI_LONG, 0, 14, comm);
}

void Communication_Hash::recvCounts(int from, int to, UTS_Strategy& nodes) {
    for(int i = from; i < to; i++) {
        long buffer;
        MPI_Recv(&buffer, 1, MPI_LONG, i, 14, comm, MPI_STATUS_IGNORE);
        nodes.addCount(buffer);
    }
}

void Communication_Hash::triggerResourceChange(UTS_Strategy &nodes) {
    int grow = evolving == 2 ? growShrink(gen) : maxProcesses - startingProcesses > 0;

    int recChange;
    if (evolving == 2) {
        recChange = calculateProcNumber(grow);
    } else {
        recChange = grow ? maxProcesses - startingProcesses : startingProcesses - maxProcesses;
    }
    char **local_input_psets, **local_output_psets;
    int local_noutput, local_op;
    MPI_Info local_info;

    if(!recChange) {
        return;
    }

    local_op = grow ? MPI_PSETOP_GROW : MPI_PSETOP_SHRINK;

    MPI_Info_create(&local_info);

    char* nprocs = itoa(recChange);

    if(local_op == MPI_PSETOP_GROW) {
        MPI_Info_set(local_info, "mpi_num_procs_add", nprocs);
    } else {
        MPI_Info_set(local_info, "mpi_num_procs_sub", nprocs);
    }
    free(nprocs);
    /* The main PSet is the input PSet of the operation */
    local_input_psets = (char **) malloc(1 * sizeof(char*));
    local_input_psets[0] = strdup(main_pset);
    local_noutput = 0;
    /* Send the Set Operation request */
    psetQueryTime = MPI_Wtime();
    if(rank == 0) {
        printf("PSet Operation and Resource Change started: %f\n", MPI_Wtime());
    }
    resourceChangeTime = psetQueryTime;
    MPI_Session_dyn_v2a_psetop(session, &local_op, local_input_psets, 1, &local_output_psets, &local_noutput, local_info);

    MPI_Info_free(&local_info);

    strcpy(delta_pset, local_output_psets[0]);

    if(MPI_PSETOP_NULL != local_op) {
        /* Publish the name of the new main PSet on the delta Pset */
        MPI_Info_create(&local_info);
        MPI_Info_set(local_info, "main_pset", local_output_psets[1]);

        MPI_Session_set_pset_data(session, local_output_psets[0], local_info);

        MPI_Info_free(&local_info);
    }
    free_string_array(local_input_psets, 1);

    if(MPI_PSETOP_NULL != local_op) {
        free_string_array(local_output_psets, local_noutput);
    }
    psetQueryTime = MPI_Wtime() - psetQueryTime;
    if(rank == 0) {
        printf("PSet Operation finished: %f\n", MPI_Wtime());
    }
    op = local_op;
    notifyResourceChange();
    MPI_Bcast(delta_pset, MPI_MAX_PSET_NAME_LEN, MPI_CHAR, 0, comm);
    MPI_Bcast(&op, 1, MPI_INT, 0, comm);

    gettingNewPset(nodes);
}

void Communication_Hash::free_string_array(char **array, int size) {
    for(int i = 0; i < size; i++){
        free(array[i]);
    }
    free(array);
}

char* Communication_Hash::itoa(int num){
    int length = snprintf( NULL, 0, "%d", num);
    char *string = (char *) malloc( length + 1 );
    snprintf( string, length + 1, "%d", num);
    return string;
}

int Communication_Hash::calculateProcNumber(int op) {
    const int possibleGrow = (maxProcesses - size) / startingProcesses;
    const int possibleShrink = (size - startingProcesses) / startingProcesses;

    if(op) {
        if(possibleGrow == 0) {
            return 0;
        } else {
            std::uniform_int_distribution<> distr(0, possibleGrow);
            int growNumber = distr(gen);
            return growNumber * startingProcesses;
        }
    } else {
        if(possibleShrink == 0) {
            return 0;
        } else {
            std::uniform_int_distribution<> distr(0, possibleShrink);
            int shrinkNumber = distr(gen);
            return shrinkNumber * startingProcesses;
        }
    }
}

void Communication_Hash::printOutTime() {
    double resultInitTime;
    MPI_Reduce(&initTime, &resultInitTime, 1, MPI_DOUBLE, MPI_MAX, 0, comm);

    if(rank == 0) {
        printf("MPI_Session_Init time for dyn Processes: %f\n", resultInitTime);
        printf("PSet Operation time: %f\n", psetQueryTime);
        printf("Resource Change time: %f\n", resourceChangeTime);
    }
}

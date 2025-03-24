/*
 * Copyright (c) 2025 Wagomu project.
 *
 * This program and the accompanying materials are made available to you under
 * the terms of the Eclipse Public License 1.0 which accompanies this
 * distribution,
 * and is available at https://www.eclipse.org/legal/epl-v10.html
 *
 * SPDX-License-Identifier: EPL-1.0
*/
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <cstring>
#include <random>
#include "mpi.h"

std::random_device rd;
std::mt19937 gen(rd());

std::uniform_int_distribution<> growShrink(0, 1);

long long monte_carlo_points_in_circle(long long num_points, unsigned int seed) {
    long long count = 0;
    srand(seed);

    for (long long i = 0; i < num_points; ++i) {
        double x = static_cast<double>(rand()) / RAND_MAX; // random x in [0, 1]
        double y = static_cast<double>(rand()) / RAND_MAX; // random y in [0, 1]
        if (x * x + y * y <= 1.0) {
            ++count;
        }
    }
    return count;
}

char *itoa(int num) {
    int length = snprintf(NULL, 0, "%d", num);
    char *string = (char *) malloc(length + 1);
    snprintf(string, length + 1, "%d", num);
    return string;
}


void free_string_array(char **array, int size) {
    for (int i = 0; i < size; i++) {
        free(array[i]);
    }
    free(array);
}

int recv_application_data(MPI_Comm comm, int *cur_iter, int *rec_change) {
    int rc;
    int buf[2];

    if (MPI_SUCCESS != (rc = MPI_Bcast(buf, 2, MPI_INT, 0, comm))) {
        return rc;
    }
    *cur_iter = buf[0];
    *rec_change = buf[1];

    return MPI_SUCCESS;
}

int send_application_data(MPI_Comm comm, int rank, int cur_iter, int recChange) {
    int rc;
    int buf[2];

    if (rank == 0) {
        buf[0] = cur_iter;
        buf[1] = recChange;
    }

    rc = MPI_Bcast(buf, 2, MPI_INT, 0, comm);

    return rc;
}

int calculateProcNumber(int usedProcesses, int nProcessesPerNode, int maxProcesses, int op) {
    const int possibleGrow = (maxProcesses - usedProcesses) / nProcessesPerNode;
    const int possibleShrink = (usedProcesses - nProcessesPerNode) / nProcessesPerNode;

    if (op) {
        if (possibleGrow == 0) {
            return 0;
        } else {
            std::uniform_int_distribution<> distr(0, possibleGrow);
            int growNumber = distr(gen);
            return growNumber * nProcessesPerNode;
        }
    } else {
        if (possibleShrink == 0) {
            return 0;
        } else {
            std::uniform_int_distribution<> distr(0, possibleShrink);
            int shrinkNumber = distr(gen);
            return shrinkNumber * nProcessesPerNode;
        }
    }
}

int filterArguments(int argc, char *argv[]) {
    if (argc != 6) {
        std::cout << "Usage: " << argv[0] <<
                " <totalPrecision> <totalIterations> <debug 1 = true, 0 = false> <starting processes> <max processes>"
                << std::endl;
        return 1;
    }
    return 0;
}

void comm_create_from_group(MPI_Session session_handle, char *pset_name, MPI_Comm *comm, int *rank) {
    MPI_Group wgroup = MPI_GROUP_NULL;

    /* create a group from pset */
    MPI_Group_from_session_pset(session_handle, pset_name, &wgroup);

    /* create a communicator from group */
    MPI_Comm_create_from_group(wgroup, "mpi.forum.example", MPI_INFO_NULL, MPI_ERRORS_RETURN, comm);

    if (MPI_COMM_NULL == *comm) {
        printf("comm_from_group returned MPI_COMM_NULL\n");
    }

    /* get the rank of this process in the new communicator */
    MPI_Comm_rank(*comm, rank);

    MPI_Group_free(&wgroup);
}

void reduceValues(long long *local_count, long long *global_count, int rank, MPI_Comm comm) {
    long long iteration_global_count = 0;

    MPI_Reduce(local_count, &iteration_global_count, 1, MPI_LONG_LONG, MPI_SUM, 0, comm);

    if (rank == 0) {
        *global_count += iteration_global_count;
        // std::cout << "Global count so far: " << *global_count << std::endl;
    }
    *local_count = 0;
}

int main(int argc, char *argv[]) {
    if (filterArguments(argc, argv)) {
        return 1;
    }
    int rank;
    long long total_points = std::stoll(argv[1]);
    int total_iterations = std::stoi(argv[2]);
    long long points_per_iteration = total_points / total_iterations;
    long long leftover_points = total_points % total_iterations;

    static bool verbose = atoi(argv[3]);
    static int nProcesses = atoi(argv[4]);
    static int maxProcesses = atoi(argv[5]);

    int num_procs = nProcesses;

    MPI_Session session = MPI_SESSION_NULL;
    MPI_Comm comm = MPI_COMM_NULL;
    MPI_Info info = MPI_INFO_NULL;
    char main_pset[MPI_MAX_PSET_NAME_LEN];
    char delta_pset[MPI_MAX_PSET_NAME_LEN];
    char boolean_string[16], **input_psets, **output_psets, host[64];
    int original_rank, flag = 0, dynamic_process, noutput, op;
    int cur_iter = 0;

    int recChange = 0;
    int grow = 1;
    bool primaryProcess;
    double t1, t2;
    long long global_count = 0;
    long long local_count = 0;

    gethostname(host, 64);
    char *dict_key = strdup("main_pset"); // The key used to store the name of the new main PSet in the PSet Dictionary

    /* We start with the mpi://WORLD PSet as main PSet */
    strcpy(main_pset, "mpi://WORLD");
    strcpy(delta_pset, "");

    /* Initialize the MPI Session */
    MPI_Session_init(MPI_INFO_NULL, MPI_ERRORS_ARE_FATAL, &session);

    /* Get the info from our mpi://WORLD pset */
    MPI_Session_get_pset_info(session, main_pset, &info);

    /* get value for the 'mpi_dyn' key -> if true, this process was added dynamically */
    MPI_Info_get(info, "mpi_dyn", 6, boolean_string, &flag);
    MPI_Info_free(&info);

    /* if mpi://WORLD is a dynamic PSet retrieve the name of the main PSet stored on mpi://WORLD */
    if ((dynamic_process = (flag && 0 == strcmp(boolean_string, "True")))) {
        /* Lookup the value for the "main_pset" key in the PSet Dictionary and use it as our main PSet */
        MPI_Session_get_pset_data(session, main_pset, main_pset, (char **) &dict_key, 1, true, &info);
        MPI_Info_get(info, "main_pset", MPI_MAX_PSET_NAME_LEN, main_pset, &flag);

        if (!flag) {
            printf("No 'next_main_pset' was provided for dynamic process. Terminate.\n");
            MPI_Session_finalize(&session);
            return -1;
        }

        MPI_Info_free(&info);
        /* Get PSet data stored on main PSet */
        MPI_Session_get_pset_info(session, main_pset, &info);
    }

    /* create a communicator from our main PSet */
    comm_create_from_group(session, main_pset, &comm, &original_rank);

    MPI_Session_get_pset_info(session, main_pset, &info);
    MPI_Info_get(info, "mpi_primary", 6, boolean_string, &flag);
    primaryProcess = (0 == strcmp(boolean_string, "True"));
    MPI_Info_free(&info);


    if (primaryProcess) {
        printf(
            "Pi_Monte_Iteration_Limitation starts with total_points=%lld, totalIterations=%d, verbose=%d, nProcesses=%d, maxProcesses=%d\n",
            total_points, total_iterations, verbose, nProcesses, maxProcesses);
        t1 = MPI_Wtime();
    }

    /* if primary process changes, we have to finalize resource change */
    if (dynamic_process) {
        recv_application_data(comm, &cur_iter, &recChange);
        if (primaryProcess) {
            MPI_Session_dyn_finalize_psetop(session, main_pset);
        }
    }

    /* Original processes will switch to a grown communicator */
    while (cur_iter < total_iterations) {
        if (primaryProcess) {
            grow = growShrink(gen);
            recChange = calculateProcNumber(num_procs, nProcesses, maxProcesses, grow);
            printf("Grow: %d, RecChange: %d\n", grow, recChange);
            send_application_data(comm, original_rank, cur_iter, recChange);
        } else {
            if (!dynamic_process) {
                recv_application_data(comm, &cur_iter, &recChange);
            } else {
                dynamic_process = false;
            }
        }

        MPI_Comm_size(comm, &num_procs);
        MPI_Comm_rank(comm, &rank);

        long long base_points = points_per_iteration / num_procs;
        long long leftover_iteration_points = points_per_iteration % num_procs;
        long long points_for_this_process = base_points;
        if (rank < leftover_iteration_points) {
            points_for_this_process++; // Distribute remainder points among the first few ranks
        }

        // Include leftover points in the last iteration
        if (cur_iter == total_iterations - 1) {
            points_for_this_process += leftover_points / num_procs;
            if (rank < leftover_points % num_procs) {
                points_for_this_process++;
            }
        }
        unsigned int seed = static_cast<unsigned int>(time(nullptr)) + rank + cur_iter;

        local_count += monte_carlo_points_in_circle(points_for_this_process, seed);
        if (verbose) {
            printf("Rank: %d, local_count: %lld\n", rank, local_count);
        }

        MPI_Barrier(comm);

        if (primaryProcess) {
            printf("Calculated with %d Processes\n", num_procs);
            printf("\n");
        }

        /* One process needs to request the set operation and publish the kickof information */
        if ((cur_iter + 1) < total_iterations && recChange) {
            if (primaryProcess) {
                /* Request the GROW operation */
                op = grow ? MPI_PSETOP_GROW : MPI_PSETOP_SHRINK;

                MPI_Info_create(&info);

                char *nprocs = itoa(recChange);
                if (op == MPI_PSETOP_GROW) {
                    MPI_Info_set(info, "mpi_num_procs_add", nprocs);
                } else {
                    MPI_Info_set(info, "mpi_num_procs_sub", nprocs);
                }
                /* The main PSet is the input PSet of the operation */
                input_psets = (char **) malloc(1 * sizeof(char *));
                input_psets[0] = strdup(main_pset);
                noutput = 0;
                /* Send the Set Operation request */
                MPI_Session_dyn_v2a_psetop(session, &op, input_psets, 1, &output_psets, &noutput, info);
                MPI_Info_free(&info);

                if (MPI_PSETOP_NULL != op) {
                    /* Publish the name of the new main PSet on the delta Pset */
                    MPI_Info_create(&info);
                    MPI_Info_set(info, "main_pset", output_psets[1]);

                    MPI_Session_set_pset_data(session, output_psets[0], info);
                    MPI_Info_free(&info);
                }
                free_string_array(input_psets, 1);

                if (MPI_PSETOP_NULL != op) {
                    free_string_array(output_psets, noutput);
                }
            }
            /* All processes can query the information about the pending Set operation */
            MPI_Session_dyn_v2a_query_psetop(session, main_pset, main_pset, &op, &output_psets, &noutput);
            if (MPI_PSETOP_NULL == op || (0 == strcmp(delta_pset, output_psets[0]))) {
                cur_iter++;
                continue;
            }

            strcpy(delta_pset, output_psets[0]);
            bool terminate = false;

            /* Is proc included in the delta PSet? If no, need to terminate */
            MPI_Session_get_pset_info(session, output_psets[0], &info);

            MPI_Info_get(info, "mpi_included", 6, boolean_string, &flag);
            if (0 == strcmp(boolean_string, "True")) {
                terminate = true;
            }
            MPI_Info_free(&info);

            /* Lookup the name of the new main PSet stored on the delta PSet */
            MPI_Session_get_pset_data(session, main_pset, output_psets[0], (char **) &dict_key, 1, true, &info);
            MPI_Info_get(info, "main_pset", MPI_MAX_PSET_NAME_LEN, main_pset, &flag);

            free_string_array(output_psets, noutput);
            MPI_Info_free(&info);

            MPI_Session_get_pset_info(session, main_pset, &info);

            MPI_Info_get(info, "mpi_primary", 6, boolean_string, &flag);
            primaryProcess = (0 == strcmp(boolean_string, "True"));
            MPI_Info_free(&info);

            reduceValues(&local_count, &global_count, original_rank, comm);

            if (terminate) {
                break;
            }
            /* Disconnect from the old communicator */
            MPI_Comm_disconnect(&comm);
            double t3;
            if (primaryProcess && verbose) {
                t3 = MPI_Wtime();
            }
            comm_create_from_group(session, main_pset, &comm, &original_rank);
            /* Indicate completion of the Pset operation*/
            if (primaryProcess && verbose) {
                double t4 = MPI_Wtime();
                printf("comm_create_from_group: %f, grow? %d\n", t4 - t3, grow);
            }

            if (primaryProcess) {
                MPI_Session_dyn_finalize_psetop(session, main_pset);
            }
        }
        cur_iter++;
    }

    reduceValues(&local_count, &global_count, original_rank, comm);

    MPI_Comm_disconnect(&comm);

    if (primaryProcess) {
        double pi = 4.0 * static_cast<double>(global_count) / static_cast<double>(total_points);
        printf("Pi: %f\n", pi);
    }

    /* Finalize the MPI Session */
    MPI_Session_finalize(&session);

    if (primaryProcess) {
        t2 = MPI_Wtime();
        printf("Elapsed time: %f\n", t2 - t1);
    }

    return 0;
}

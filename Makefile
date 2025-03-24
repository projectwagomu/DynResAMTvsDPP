# ----------------------------------------
# Compiler and Flags
# ----------------------------------------
CC       := mpicxx
CXX      := g++
CFLAGS   := -O3 -march=native -flto -Isrc
LDFLAGS  := -lssl -lcrypto

# ----------------------------------------
# Directories and Paths
# ----------------------------------------
SRC_DIR     := src
PI_SRC_DIR  := $(SRC_DIR)/pi
UTS_SRC_DIR := $(SRC_DIR)/uts
OUT_DIR     := $(CURDIR)/out

# ----------------------------------------
# Source and Target Files
# ----------------------------------------
PI_SRC      := $(PI_SRC_DIR)/pi_monte_iteration_limitation.cpp
PI_EXEC     := $(OUT_DIR)/pi_monte_iteration_limitation

UTS_MAIN    := $(UTS_SRC_DIR)/UTS_mpi_stealing_hash.cpp
UTS_OBJS    := $(OUT_DIR)/UTS_Strategy.o \
               $(OUT_DIR)/Communication_Hash.o
UTS_EXEC    := $(OUT_DIR)/uts_mpi_stealing_hash

# ----------------------------------------
# Default Target
# ----------------------------------------
all: build

build: $(OUT_DIR) $(PI_EXEC) $(UTS_EXEC)

# ----------------------------------------
# Build Rules
# ----------------------------------------

# Compile Pi Monte Carlo executable
$(PI_EXEC): $(PI_SRC)
	$(CC) $(CFLAGS) $< -o $@

# Compile UTS object files
$(OUT_DIR)/UTS_Strategy.o: $(UTS_SRC_DIR)/UTS_Strategy.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/Communication_Hash.o: $(UTS_SRC_DIR)/Communication_Hash.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# Link UTS executable
$(UTS_EXEC): $(UTS_MAIN) $(UTS_OBJS)
	$(CC) $(CFLAGS) $< -o $@ $(UTS_OBJS) $(LDFLAGS)

# ----------------------------------------
# Create output directory if needed
# ----------------------------------------
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# ----------------------------------------
# Run Targets
# ----------------------------------------

# Run Pi Monte Carlo
# Args: total_points total_iterations verbose nProcesses maxProcesses
pi_monte_iter: build
	mpirun -np 8 --display map \
		--host n1:8,n2:8,n3:8,n4:8,n5:8,n6:8,n7:8,n8:8 \
		$(PI_EXEC) 1000000000 1 0 8 8

# Run UTS with MPI stealing + hash strategy
# Args: depth evolvingFlag seconds startingProcesses maxProcesses
uts_mpi_stealing_hash: build
	mpirun -np 2 --display map \
		--host n1:2,n2:2,n3:2,n4:2,n5:2,n6:2,n7:2,n8:2 \
		$(UTS_EXEC) 14 1 5 2 4

# ----------------------------------------
# Clean Target
# ----------------------------------------
clean:
	rm -rf $(OUT_DIR)

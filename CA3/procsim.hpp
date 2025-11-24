#ifndef PROCSIM_HPP
#define PROCSIM_HPP

#include <cstdint>
#include <cstdio>

#define DEFAULT_K0 1
#define DEFAULT_K1 2
#define DEFAULT_K2 3
#define DEFAULT_R 8
#define DEFAULT_F 4

typedef struct _proc_inst_t
{
    uint32_t instruction_address;
    int32_t op_code;
    int32_t src_reg[2];
    int32_t dest_reg;
    
    // You may introduce other fields as needed
    // ============== DEBUG: REMOVE BEFORE SUBMISSION ==============
    uint64_t inst_num;       // Instruction number (for tracking)
    uint64_t fetch_cycle;    // Cycle when instruction was fetched
    uint64_t disp_cycle;     // Cycle when instruction was dispatched
    uint64_t sched_cycle;    // Cycle when instruction became schedulable (sources ready)
    uint64_t exec_cycle;     // Cycle when instruction fired/executed
    uint64_t state_cycle;    // Cycle when instruction retired/completed
    // ============== END DEBUG ==============
    
} proc_inst_t;

typedef struct _proc_stats_t
{
    float avg_inst_retired;
    float avg_inst_fired;
    float avg_disp_size;
    unsigned long max_disp_size;
    unsigned long retired_instruction;
    unsigned long cycle_count;
} proc_stats_t;

bool read_instruction(proc_inst_t* p_inst);

void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f);
void run_proc(proc_stats_t* p_stats);
void complete_proc(proc_stats_t* p_stats);
void print_instruction_trace();  // DEBUG: REMOVE BEFORE SUBMISSION

#endif /* PROCSIM_HPP */

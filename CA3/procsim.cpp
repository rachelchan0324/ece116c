#include "procsim.hpp"
#include <vector>
#include <algorithm>

// configuration parameters
uint64_t num_result_buses;           // nnmber of result buses (r)
uint64_t num_k0_fus;                 // number of k0 functional units
uint64_t num_k1_fus;                 // number of k1 functional units
uint64_t num_k2_fus;                 // number of k2 functional units
uint64_t fetch_width;                // number of instructions to fetch per cycle (f)
// functional unit: just tracks if busy (latency is always 1)
struct FunctionalUnit {
    bool busy;
};

// functional unit arrays (one for each type)
std::vector<FunctionalUnit> k0_units;
std::vector<FunctionalUnit> k1_units;
std::vector<FunctionalUnit> k2_units;

// dispatch queue - holds instructions waiting to be executed
std::vector<proc_inst_t> dispatch_queue;

// result buses - broadcast completed instruction tags
struct ResultBus {
    bool busy;
    uint64_t tag;
};
std::vector<ResultBus> result_buses;

// fetch queue - holds freshly fetched instructions
std::vector<proc_inst_t> fetch_queue;

// scheduling queue - combined RS + ROB (size = 2 * (k0 + k1 + k2))
struct ScheduleEntry {
    proc_inst_t instruction;
    uint64_t tag;
    bool fired;
    int fu_index;
    int fu_type;
    uint64_t completion_cycle;
    bool broadcast;  // true if result has been broadcast on CDB
    bool src0_ready;
    uint64_t src0_tag;
    bool src1_ready;
    uint64_t src1_tag;
};

std::vector<ScheduleEntry> schedule_queue;
uint64_t next_tag = 1;
uint64_t schedule_queue_size = 0;

// register scoreboard
std::vector<bool> register_ready;
std::vector<uint64_t> register_tag;  // tag of the producer instruction for each register

std::vector<proc_inst_t> completed_instructions;
uint64_t inst_counter = 0;

// statistics
uint64_t total_dispatch_size = 0;
uint64_t total_inst_fired = 0;
uint64_t total_inst_retired = 0;
uint64_t cycle_count = 0;
uint64_t max_dispatch_size = 0;
bool fetch_complete = false;

/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @r number of result busses
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 */
void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f) 
{
    num_result_buses = r;
    num_k0_fus = k0;
    num_k1_fus = k1;
    num_k2_fus = k2;
    fetch_width = f;
    
    k0_units.resize(k0);
    k1_units.resize(k1);
    k2_units.resize(k2);
    for (auto& unit : k0_units) unit.busy = false;
    for (auto& unit : k1_units) unit.busy = false;
    for (auto& unit : k2_units) unit.busy = false;
    
    register_ready.resize(128, true);
    register_tag.resize(128, 0);
    fetch_queue.clear();
    dispatch_queue.clear();
    schedule_queue_size = 2 * (k0 + k1 + k2);
    schedule_queue.clear();
    result_buses.resize(r);
    for (auto& bus : result_buses) bus.busy = false;
    
    total_dispatch_size = 0;
    total_inst_fired = 0;
    total_inst_retired = 0;
    cycle_count = 0;
    max_dispatch_size = 0;
    next_tag = 1;
    fetch_complete = false;
    completed_instructions.clear();
    inst_counter = 0;
}

// FETCH: fetch up to fetch_width instructions and add to fetch queue
void fetch_phase()
{
    if (fetch_complete) return;
    
    for (uint64_t i = 0; i < fetch_width; i++) {
        proc_inst_t inst;
        if (read_instruction(&inst)) {
            inst.inst_num = ++inst_counter;
            inst.fetch_cycle = cycle_count;  // instruction fetched in current cycle
            inst.disp_cycle = 0;
            inst.sched_cycle = 0;
            inst.exec_cycle = 0;
            inst.state_cycle = 0;
            fetch_queue.push_back(inst);
        } else {
            fetch_complete = true;
            break;
        }
    }
}

// DISPATCH: move instructions from fetch queue to dispatch queue
void dispatch_phase()
{
    while (!fetch_queue.empty()) {
        proc_inst_t inst = fetch_queue.front();
        inst.disp_cycle = cycle_count;
        
        dispatch_queue.push_back(inst);
        fetch_queue.erase(fetch_queue.begin());
    }
}

// SCHEDULE: move instructions from dispatch queue to schedule queue
void schedule_phase()
{
    // move instructions from dispatch queue to schedule queue (if space available)
    while (!dispatch_queue.empty() && schedule_queue.size() < schedule_queue_size) {
        proc_inst_t inst = dispatch_queue.front();
        inst.sched_cycle = cycle_count;  // instruction scheduled in current cycle
        
        ScheduleEntry entry;
        entry.instruction = inst;
        entry.tag = next_tag++;
        entry.fired = false;
        entry.broadcast = false;
        entry.fu_index = -1;
        entry.fu_type = -1;
        entry.completion_cycle = 0;
        
        // copy ready bits and tags from register file
        if (inst.src_reg[0] != -1) {
            entry.src0_ready = register_ready[inst.src_reg[0]];
            entry.src0_tag = register_tag[inst.src_reg[0]];
        } else {
            entry.src0_ready = true;  // no dependency
            entry.src0_tag = 0;
        }      
        if (inst.src_reg[1] != -1) {
            entry.src1_ready = register_ready[inst.src_reg[1]];
            entry.src1_tag = register_tag[inst.src_reg[1]];
        } else {
            entry.src1_ready = true;
            entry.src1_tag = 0;
        }
        
        // update register file: mark dest as not ready and assign new tag
        if (inst.dest_reg != -1) {
            register_ready[inst.dest_reg] = false;
            register_tag[inst.dest_reg] = entry.tag;
        }
        
        dispatch_queue.erase(dispatch_queue.begin());
        schedule_queue.push_back(entry);
    }
}

void execute_phase()
{
    // STEP 1: Fire ready instructions into available FUs
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (schedule_queue[i].fired) continue;
        
        proc_inst_t& inst = schedule_queue[i].instruction;
        
        // check if ready to fire (both sources must be ready)
        if (!schedule_queue[i].src0_ready || !schedule_queue[i].src1_ready) continue;
        
        // find available FU
        std::vector<FunctionalUnit>* units = nullptr;
        int fu_type = -1;
        
        if (inst.op_code == 0) { units = &k0_units; fu_type = 0; }
        else if (inst.op_code == -1 || inst.op_code == 1) { units = &k1_units; fu_type = 1; }
        else if (inst.op_code == 2) { units = &k2_units; fu_type = 2; }
        
        if (units) {
            for (size_t j = 0; j < units->size(); j++) {
                if (!(*units)[j].busy) {
                    // fire instruction
                    schedule_queue[i].fired = true;
                    schedule_queue[i].fu_index = j;
                    schedule_queue[i].fu_type = fu_type;
                    schedule_queue[i].completion_cycle = cycle_count;  // latency=1 means completes same cycle
                    schedule_queue[i].instruction.exec_cycle = cycle_count;
                    
                    (*units)[j].busy = true;
                    
                    total_inst_fired++;
                    break;
                }
            }
        }
    }
    
    // STEP 2: find completed instructions and broadcast on CDB (result buses)
    std::vector<size_t> completed_indices;
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (!schedule_queue[i].fired) continue;
        if (schedule_queue[i].completion_cycle <= cycle_count) {
            completed_indices.push_back(i);
        }
    }
    
    // sort by tag (lowest first) for in-order retirement
    std::sort(completed_indices.begin(), completed_indices.end(), [](size_t a, size_t b) {
        return schedule_queue[a].tag < schedule_queue[b].tag;
    });
    
    // broadcast up to num_result_buses completed instructions on CDB
    uint64_t broadcast_count = 0;
    for (size_t idx : completed_indices) {
        if (broadcast_count >= num_result_buses) break;
        
        ScheduleEntry& entry = schedule_queue[idx];
        
        // broadcast on result bus
        result_buses[broadcast_count].busy = true;
        result_buses[broadcast_count].tag = entry.tag;
        entry.broadcast = true;  // mark as broadcast
        
        // free the functional unit
        if (entry.fu_type == 0) {
            k0_units[entry.fu_index].busy = false;
        } else if (entry.fu_type == 1) {
            k1_units[entry.fu_index].busy = false;
        } else if (entry.fu_type == 2) {
            k2_units[entry.fu_index].busy = false;
        }
        
        broadcast_count++;
    }
    
    // STEP 3: wakeup - CDB updates scheduling queue (update dependencies from result buses)
    for (auto& entry : schedule_queue) {
        if (entry.fired) continue;
        
        for (auto& bus : result_buses) {
            if (bus.busy) {
                if (entry.src0_tag == bus.tag) {
                    entry.src0_tag = 0;
                    entry.src0_ready = true;
                }
                if (entry.src1_tag == bus.tag) {
                    entry.src1_tag = 0;
                    entry.src1_ready = true;
                }
            }
        }
    }
    
    // Register file update happens at the beginning of next cycle in run_proc()
    // Result buses are also cleared there
}

// STATE UPDATE: retire completed instructions (remove from schedule queue)
void state_update_phase()
{
    // find instructions that have been broadcast on CDB and are ready to retire
    std::vector<size_t> to_remove;
    
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (!schedule_queue[i].broadcast) continue;  // only retire if already broadcast
        
        ScheduleEntry& entry = schedule_queue[i];
        entry.instruction.state_cycle = cycle_count;
        completed_instructions.push_back(entry.instruction);
        
        to_remove.push_back(i);
        total_inst_retired++;
    }
    
    // remove retired instructions from schedule queue (reverse order to maintain indices)
    for (int i = to_remove.size() - 1; i >= 0; i--) {
        schedule_queue.erase(schedule_queue.begin() + to_remove[i]);
    }
}

/**
 * Subroutine that simulates the processor.
 *   The processor should fetch instructions as appropriate, until all instructions have executed
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void run_proc(proc_stats_t* p_stats)
{
    while (true) {
        cycle_count++;
        
        // STEP 0: Update register file from PREVIOUS cycle's CDB broadcasts
        for (auto& bus : result_buses) {
            if (bus.busy) {
                for (int reg = 0; reg < 128; reg++) {
                    if (register_tag[reg] == bus.tag) {
                        register_ready[reg] = true;
                    }
                }
            }
        }
        // Clear CDB after updating register file
        for (auto& bus : result_buses) bus.busy = false;
        
        state_update_phase();      // retire completed instructions
        execute_phase();           // fire instructions, broadcast results (but don't update register file yet)
        schedule_phase();          // move from dispatch queue to schedule queue (sees register file from previous cycle)
        dispatch_phase();          // move from fetch queue to dispatch queue
        fetch_phase();             // fetch new instructions to fetch queue
        
        total_dispatch_size += dispatch_queue.size();
        if (dispatch_queue.size() > max_dispatch_size) max_dispatch_size = dispatch_queue.size();
        
        if (fetch_complete && fetch_queue.empty() && dispatch_queue.empty() && schedule_queue.empty()) break;
        
        if (cycle_count > 200000) {
            fprintf(stderr, "ERROR: Exceeded 200k cycles - likely deadlock!\n");
            fprintf(stderr, "fetch_complete=%d, fetch_queue=%zu, dispatch_queue=%zu, schedule_queue=%zu\n",
                   fetch_complete, fetch_queue.size(), dispatch_queue.size(), schedule_queue.size());
            fprintf(stderr, "Dumping first 5 schedule queue entries:\n");
            for (size_t i = 0; i < 5 && i < schedule_queue.size(); i++) {
                fprintf(stderr, "  [%zu] tag=%llu fired=%d src0_tag=%llu src1_tag=%llu\n",
                       i, (unsigned long long)schedule_queue[i].tag, schedule_queue[i].fired,
                       (unsigned long long)schedule_queue[i].src0_tag, (unsigned long long)schedule_queue[i].src1_tag);
            }
            break;
        }
    }
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_proc(proc_stats_t *p_stats) 
{
    p_stats->avg_inst_retired = (float)total_inst_retired / (float)cycle_count;
    p_stats->avg_inst_fired = (float)total_inst_fired / (float)cycle_count;
    p_stats->avg_disp_size = (float)total_dispatch_size / (float)cycle_count;
    p_stats->max_disp_size = max_dispatch_size;
    p_stats->retired_instruction = total_inst_retired;
    p_stats->cycle_count = cycle_count;
}

// ============== DEBUG: REMOVE BEFORE SUBMISSION ==============
void print_instruction_trace()
{
    printf("INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");
    for (const auto& inst : completed_instructions) {
        printf("%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",
               (unsigned long long)inst.inst_num,
               (unsigned long long)inst.fetch_cycle,
               (unsigned long long)inst.disp_cycle,
               (unsigned long long)inst.sched_cycle,
               (unsigned long long)inst.exec_cycle,
               (unsigned long long)inst.state_cycle);
    }
}
// ============== END DEBUG ==============
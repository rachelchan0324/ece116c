#include "procsim.hpp"
#include <vector>

// Configuration parameters
uint64_t num_result_buses;           // Number of result buses (r)
uint64_t num_k0_fus;                 // Number of k0 functional units
uint64_t num_k1_fus;                 // Number of k1 functional units
uint64_t num_k2_fus;                 // Number of k2 functional units
uint64_t fetch_width;                // Number of instructions to fetch per cycle (f)

// Functional Unit - tracks if FU is busy and how many cycles remain
struct FunctionalUnit {
    bool busy;                       // Is this FU currently executing an instruction?
    int cycles_remaining;            // How many more cycles until instruction completes?
    proc_inst_t instruction;         // The instruction being executed
};

// Functional unit arrays (one for each type)
std::vector<FunctionalUnit> k0_units;  // op_code == -1, 1 cycle latency
std::vector<FunctionalUnit> k1_units;  // op_code == 0,  2 cycle latency
std::vector<FunctionalUnit> k2_units;  // op_code == 1,  3 cycle latency

// Dispatch queue - holds instructions waiting to be executed
std::vector<proc_inst_t> dispatch_queue;

// Scheduling queue - combined RS + ROB (size = 2 * (k0 + k1 + k2))
struct ScheduleEntry {
    proc_inst_t instruction;         // The instruction
    uint64_t tag;                    // Tag for program order (1, 2, 3, ...)
    bool fired;                      // Has this instruction been issued to an FU?
    bool completed;                  // Has execution finished?
    int fu_index;                    // Which FU is executing this (-1 if not fired)
    int fu_type;                     // 0=k0, 1=k1, 2=k2
};

std::vector<ScheduleEntry> schedule_queue;
uint64_t next_tag = 1;               // Tag counter for instructions
uint64_t schedule_queue_size = 0;    // Max size of scheduling queue

// Register scoreboard - tracks which registers are ready
// register_ready[i] = true means register i is ready to be read
// register_ready[i] = false means register i is waiting for a write
std::vector<bool> register_ready;

// Statistics tracking (cumulative values across all cycles)
uint64_t total_dispatch_size = 0;     // Sum of dispatch queue sizes each cycle
uint64_t total_inst_fired = 0;        // Total instructions fired to FUs
uint64_t total_inst_retired = 0;      // Total instructions completed
uint64_t cycle_count = 0;             // Current cycle number
uint64_t max_dispatch_size = 0;       // Maximum dispatch queue size seen

// Track if we've finished fetching all instructions
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
    // Store configuration parameters in global variables
    num_result_buses = r;
    num_k0_fus = k0;
    num_k1_fus = k1;
    num_k2_fus = k2;
    fetch_width = f;
    
    // Initialize k0, k1, k2 functional units
    k0_units.resize(k0);
    for (uint64_t i = 0; i < k0; i++) {
        k0_units[i].busy = false;           // Start with all FUs idle
        k0_units[i].cycles_remaining = 0;   // No cycles remaining
    }
    k1_units.resize(k1);
    for (uint64_t i = 0; i < k1; i++) {
        k1_units[i].busy = false;
        k1_units[i].cycles_remaining = 0;
    }
    k2_units.resize(k2);
    for (uint64_t i = 0; i < k2; i++) {
        k2_units[i].busy = false;
        k2_units[i].cycles_remaining = 0;
    }
    
    // Initialize register scoreboard, 128 registers (typical RISC architecture)
    register_ready.resize(128, true);  // All registers start as "ready"
    
    // Clear dispatch queue and reserve some space for efficiency
    dispatch_queue.clear();
    
    // Initialize statistics counters
    total_dispatch_size = 0;
    total_inst_fired = 0;
    total_inst_retired = 0;
    cycle_count = 0;
    max_dispatch_size = 0;
    
    // Calculate and initialize scheduling queue size
    schedule_queue_size = 2 * (k0 + k1 + k2);
    schedule_queue.clear();
    next_tag = 1;
    
    // Reset fetch completion flag
    fetch_complete = false;
}

// FETCH PHASE
// fetch up to `fetch_width` instructions and append them to the dispatch queue
// sets `fetch_complete` when the trace has no more instructions
void fetch_phase()
{
    if (fetch_complete) return;

    for (uint64_t i = 0; i < fetch_width; i++) {
        proc_inst_t inst;
        if (read_instruction(&inst)) {
            dispatch_queue.push_back(inst);
        } else {
            fetch_complete = true;
            break;
        }
    }
}

// DISPATCH PHASE
// move instructions from dispatch queue to schedule queue (reservation station).
// - dispatch queue has unlimited size (simplified)
// - schedule queue has limited size: 2 * (k0 + k1 + k2)
// - assign tags to instructions (tag counter starts at 1)
// - scan dispatch queue from head to tail (program order)
void dispatch_phase()
{
    // scan from front to back (program order)
    while (!dispatch_queue.empty() && schedule_queue.size() < schedule_queue_size) {
        // take the first instruction from dispatch queue
        proc_inst_t inst = dispatch_queue.front();
        dispatch_queue.erase(dispatch_queue.begin());
        
        // create a schedule entry with a tag
        ScheduleEntry entry;
        entry.instruction = inst;
        entry.tag = next_tag++;           // Assign sequential tag (1, 2, 3, ...)
        entry.fired = false;              // Not yet issued to FU
        entry.fu_index = -1;              // No FU assigned yet
        entry.fu_type = -1;               // No FU type assigned yet
        
        // add to schedule queue (reservation station)
        schedule_queue.push_back(entry);
    }
}

// STATE UPDATE (execute): decrement cycles remaining on all busy FUs
void state_update_phase()
{
    for (size_t i = 0; i < k0_units.size(); i++) {
        if (k0_units[i].busy && k0_units[i].cycles_remaining > 0) {
            k0_units[i].cycles_remaining--;
        }
    }
    for (size_t i = 0; i < k1_units.size(); i++) {
        if (k1_units[i].busy && k1_units[i].cycles_remaining > 0) {
            k1_units[i].cycles_remaining--;
        }
    }
    for (size_t i = 0; i < k2_units.size(); i++) {
        if (k2_units[i].busy && k2_units[i].cycles_remaining > 0) {
            k2_units[i].cycles_remaining--;
        }
    }
}

// SCHEDULE/FIRE (execute): issue ready instructions to available FUs (in tag order)
void schedule_fire_phase()
{
    // scan schedule queue in tag order (already sorted by insertion order)
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (schedule_queue[i].fired) continue;  // already executing
        
        proc_inst_t& inst = schedule_queue[i].instruction;
        
        // check if source registers are ready (register -1 is always ready)
        bool src0_ready = (inst.src_reg[0] == -1) || register_ready[inst.src_reg[0]];
        bool src1_ready = (inst.src_reg[1] == -1) || register_ready[inst.src_reg[1]];
        
        if (!src0_ready || !src1_ready) continue;  // dependencies not met
        
        // find available FU based on op_code
        int fu_index = -1;
        int fu_type = -1;
        int latency = 1;
        
        if (inst.op_code == -1 || inst.op_code == 0) {
            // k0 units (type 0, 1 cycle latency)
            for (size_t j = 0; j < k0_units.size(); j++) {
                if (!k0_units[j].busy) {
                    fu_index = j;
                    fu_type = 0;
                    latency = 1;
                    break;
                }
            }
        } else if (inst.op_code == 1) {
            // k1 units (type 1, 1 cycle latency)
            for (size_t j = 0; j < k1_units.size(); j++) {
                if (!k1_units[j].busy) {
                    fu_index = j;
                    fu_type = 1;
                    latency = 1;
                    break;
                }
            }
        } else if (inst.op_code == 2) {
            // k2 units (type 2, 1 cycle latency)
            for (size_t j = 0; j < k2_units.size(); j++) {
                if (!k2_units[j].busy) {
                    fu_index = j;
                    fu_type = 2;
                    latency = 1;
                    break;
                }
            }
        }
        
        // if FU is available, fire the instruction
        if (fu_index != -1) {
            schedule_queue[i].fired = true;
            schedule_queue[i].fu_index = fu_index;
            schedule_queue[i].fu_type = fu_type;
            
            // mark FU as busy
            if (fu_type == 0) {
                k0_units[fu_index].busy = true;
                k0_units[fu_index].cycles_remaining = latency;
                k0_units[fu_index].instruction = inst;
            } else if (fu_type == 1) {
                k1_units[fu_index].busy = true;
                k1_units[fu_index].cycles_remaining = latency;
                k1_units[fu_index].instruction = inst;
            } else if (fu_type == 2) {
                k2_units[fu_index].busy = true;
                k2_units[fu_index].cycles_remaining = latency;
                k2_units[fu_index].instruction = inst;
            }
            
            // mark destination register as not ready (register -1 means no register)
            if (inst.dest_reg != -1) {
                register_ready[inst.dest_reg] = false;
            }
            
            total_inst_fired++;
        }
    }
}

// COMPLETE/RETIRE (execute): retire completed instructions via result buses (tag order)
void complete_retire_phase()
{
    // find all instructions that have completed execution (cycles_remaining == 0)
    std::vector<size_t> completed_indices;
    
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        // skip if not yet fired
        if (!schedule_queue[i].fired) continue;
        
        int fu_idx = schedule_queue[i].fu_index;
        int fu_type = schedule_queue[i].fu_type;
        
        // check if FU has finished (cycles_remaining == 0)
        bool finished = false;
        if (fu_type == 0 && k0_units[fu_idx].busy && k0_units[fu_idx].cycles_remaining == 0) {
            finished = true;
        } else if (fu_type == 1 && k1_units[fu_idx].busy && k1_units[fu_idx].cycles_remaining == 0) {
            finished = true;
        } else if (fu_type == 2 && k2_units[fu_idx].busy && k2_units[fu_idx].cycles_remaining == 0) {
            finished = true;
        }
        
        if (finished) {
            completed_indices.push_back(i);
        }
    }
    
    // sort completed instructions by tag (lowest first)
    for (size_t i = 0; i < completed_indices.size(); i++) {
        for (size_t j = i + 1; j < completed_indices.size(); j++) {
            if (schedule_queue[completed_indices[j]].tag < schedule_queue[completed_indices[i]].tag) {
                size_t temp = completed_indices[i];
                completed_indices[i] = completed_indices[j];
                completed_indices[j] = temp;
            }
        }
    }
    
    // retire up to 'num_result_buses' instructions (result bus limit)
    uint64_t retired_count = 0;
    std::vector<size_t> to_remove;
    
    for (size_t idx : completed_indices) {
        if (retired_count >= num_result_buses) break;
        
        ScheduleEntry& entry = schedule_queue[idx];
        
        // mark destination register as ready (register -1 means no register)
        if (entry.instruction.dest_reg != -1) {
            register_ready[entry.instruction.dest_reg] = true;
        }
        
        // free the functional unit
        if (entry.fu_type == 0) {
            k0_units[entry.fu_index].busy = false;
        } else if (entry.fu_type == 1) {
            k1_units[entry.fu_index].busy = false;
        } else if (entry.fu_type == 2) {
            k2_units[entry.fu_index].busy = false;
        }
        
        to_remove.push_back(idx);
        retired_count++;
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
        
        // execute phases in proper order (per spec timing)
        state_update_phase();      // decrement FU counters
        complete_retire_phase();   // retire completed instructions (via result buses)
        schedule_fire_phase();     // fire ready instructions to FUs
        dispatch_phase();          // move from dispatch queue to schedule queue
        fetch_phase();             // fetch new instructions
        
        // track statistics each cycle
        total_dispatch_size += dispatch_queue.size();
        if (dispatch_queue.size() > max_dispatch_size) {
            max_dispatch_size = dispatch_queue.size();
        }
        
        // check for termination
        if (fetch_complete && dispatch_queue.empty() && schedule_queue.empty()) {
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
    // clculate averages from cumulative totals
    p_stats->avg_inst_retired = (float)total_inst_retired / (float)cycle_count;
    p_stats->avg_inst_fired = (float)total_inst_fired / (float)cycle_count;
    p_stats->avg_disp_size = (float)total_dispatch_size / (float)cycle_count;
    
    // fill in the rest of the statistics
    p_stats->max_disp_size = max_dispatch_size;
    p_stats->retired_instruction = total_inst_retired;
    p_stats->cycle_count = cycle_count;
}
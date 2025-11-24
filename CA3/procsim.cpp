#include "procsim.hpp"
#include <vector>

// configuration parameters
uint64_t num_result_buses;           // Number of result buses (r)
uint64_t num_k0_fus;                 // Number of k0 functional units
uint64_t num_k1_fus;                 // Number of k1 functional units
uint64_t num_k2_fus;                 // Number of k2 functional units
uint64_t fetch_width;                // Number of instructions to fetch per cycle (f)

// Functional Unit: just tracks if busy (latency is always 1)
struct FunctionalUnit {
    bool busy;
};

// Functional unit arrays (one for each type)
std::vector<FunctionalUnit> k0_units;  // op_code == -1, 1 cycle latency
std::vector<FunctionalUnit> k1_units;  // op_code == 0,  2 cycle latency
std::vector<FunctionalUnit> k2_units;  // op_code == 1,  3 cycle latency

// Dispatch queue - holds instructions waiting to be executed
std::vector<proc_inst_t> dispatch_queue;

// Result buses - broadcast completed instruction tags
struct ResultBus {
    bool busy;
    uint64_t tag;
};
std::vector<ResultBus> result_buses;

// Scheduling queue - combined RS + ROB (size = 2 * (k0 + k1 + k2))
struct ScheduleEntry {
    proc_inst_t instruction;
    uint64_t tag;
    bool fired;
    int fu_index;
    int fu_type;
    uint64_t completion_cycle;
    uint64_t src0_tag;
    uint64_t src1_tag;
};

std::vector<ScheduleEntry> schedule_queue;
uint64_t next_tag = 1;
uint64_t schedule_queue_size = 0;

// Register scoreboard
std::vector<bool> register_ready;
std::vector<uint64_t> register_tag;  // Tag of the producer instruction for each register

// Statistics
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
}

// FETCH: fetch up to fetch_width instructions and add to dispatch queue
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

// DISPATCH: move instructions from dispatch queue to schedule queue
void dispatch_phase()
{
    while (!dispatch_queue.empty() && schedule_queue.size() < schedule_queue_size) {
        proc_inst_t inst = dispatch_queue.front();
        ScheduleEntry entry;
        entry.instruction = inst;
        entry.tag = next_tag++;
        entry.fired = false;
        entry.fu_index = -1;
        entry.fu_type = -1;
        entry.completion_cycle = 0;
        
        // Set source tags: 0 if ready, otherwise the tag of the producer instruction
        if (inst.src_reg[0] == -1 || register_ready[inst.src_reg[0]]) {
            entry.src0_tag = 0;  // Ready
        } else {
            entry.src0_tag = register_tag[inst.src_reg[0]];  // Wait for producer
        }
        
        if (inst.src_reg[1] == -1 || register_ready[inst.src_reg[1]]) {
            entry.src1_tag = 0;  // Ready
        } else {
            entry.src1_tag = register_tag[inst.src_reg[1]];  // Wait for producer
        }
        
        // Mark dest register as not ready and record this instruction as the producer
        if (inst.dest_reg != -1) {
            register_ready[inst.dest_reg] = false;
            register_tag[inst.dest_reg] = entry.tag;
        }
        
        dispatch_queue.erase(dispatch_queue.begin());
        schedule_queue.push_back(entry);
    }
}

// SCHEDULE: watch result buses and update dependent instructions
void schedule_phase()
{
    for (auto& entry : schedule_queue) {
        if (entry.fired) continue;
        for (auto& bus : result_buses) {
            if (bus.busy) {
                if (entry.src0_tag == bus.tag) entry.src0_tag = 0;
                if (entry.src1_tag == bus.tag) entry.src1_tag = 0;
            }
        }
    }
    for (auto& bus : result_buses) bus.busy = false;
}

void execute_phase()
{
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (schedule_queue[i].fired) continue;
        
        proc_inst_t& inst = schedule_queue[i].instruction;
        
        // Check if ready to fire (src tags are 0 = ready)
        if (schedule_queue[i].src0_tag != 0 || schedule_queue[i].src1_tag != 0) continue;
        
        // Find available FU
        std::vector<FunctionalUnit>* units = nullptr;
        int fu_type = -1;
        
        if (inst.op_code == -1 || inst.op_code == 0) { units = &k0_units; fu_type = 0; }
        else if (inst.op_code == 1) { units = &k1_units; fu_type = 1; }
        else if (inst.op_code == 2) { units = &k2_units; fu_type = 2; }
        
        if (units) {
            for (size_t j = 0; j < units->size(); j++) {
                if (!(*units)[j].busy) {
                    // Fire instruction
                    schedule_queue[i].fired = true;
                    schedule_queue[i].fu_index = j;
                    schedule_queue[i].fu_type = fu_type;
                    schedule_queue[i].completion_cycle = cycle_count + 1;
                    
                    (*units)[j].busy = true;
                    
                    total_inst_fired++;
                    break;
                }
            }
        }
    }
}

// STATE UPDATE: retire completed instructions and broadcast on result buses
void state_update_phase()
{
    // find all instructions that completed (fired in previous cycle, since latency=1)
    std::vector<size_t> completed_indices;
    
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        if (!schedule_queue[i].fired) continue;
        
        // with latency=1, instruction completes when completion_cycle <= current cycle
        if (schedule_queue[i].completion_cycle <= cycle_count) {
            completed_indices.push_back(i);
        }
    }
    
    // Sort by tag (lowest first)
    std::sort(completed_indices.begin(), completed_indices.end(), [](size_t a, size_t b) {
        return schedule_queue[a].tag < schedule_queue[b].tag;
    });
    
    // retire up to 'num_result_buses' instructions (result bus limit)
    uint64_t retired_count = 0;
    std::vector<size_t> to_remove;
    
    for (size_t idx : completed_indices) {
        if (retired_count >= num_result_buses) break;
        
        ScheduleEntry& entry = schedule_queue[idx];
        
        // Broadcast on result bus
        result_buses[retired_count].busy = true;
        result_buses[retired_count].tag = entry.tag;
        
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
        
        if (cycle_count % 1000 == 0) {
            fprintf(stderr, "Cycle %llu: dq=%zu sq=%zu retired=%llu fc=%d\n",
                   (unsigned long long)cycle_count, dispatch_queue.size(), schedule_queue.size(),
                   (unsigned long long)total_inst_retired, fetch_complete);
        }
        
        state_update_phase();   // retire & broadcast on result buses
        schedule_phase();       // watch buses & update dependencies
        execute_phase();        // fire ready instructions
        dispatch_phase();
        fetch_phase();
        
        total_dispatch_size += dispatch_queue.size();
        if (dispatch_queue.size() > max_dispatch_size) max_dispatch_size = dispatch_queue.size();
        
        if (fetch_complete && dispatch_queue.empty() && schedule_queue.empty()) break;
        
        if (cycle_count > 200000) {
            fprintf(stderr, "ERROR: Exceeded 200k cycles - likely deadlock!\n");
            fprintf(stderr, "fetch_complete=%d, dispatch_queue=%zu, schedule_queue=%zu\n",
                   fetch_complete, dispatch_queue.size(), schedule_queue.size());
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
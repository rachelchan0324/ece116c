#include "procsim.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <deque>

// configuration parameters
uint64_t num_result_buses;           // nnmber of result buses (r)
uint64_t num_k0_fus;                 // number of k0 functional units
uint64_t num_k1_fus;                 // number of k1 functional units
uint64_t num_k2_fus;                 // number of k2 functional units
uint64_t fetch_width;                // number of instructions to fetch per cycle (f)

// functional unit: tracks busy state and cycles remaining
struct FunctionalUnit {
    bool busy;
    int cycles;  // cycles remaining for current instruction
    uint64_t current_tag;  // tag of instruction using this FU
};

// functional unit arrays (one for each type)
std::vector<FunctionalUnit> k0_units;
std::vector<FunctionalUnit> k1_units;
std::vector<FunctionalUnit> k2_units;

// dispatch queue - holds instructions waiting to be executed
std::vector<proc_inst_t> dispatch_queue;

// result bus queue - holds tags of completed instructions ready to broadcast/retire
std::deque<uint64_t> result_bus_queue;

// retired queue - holds tags of instructions that were retired in previous cycle, to be removed from schedule queue
std::vector<uint64_t> retired_queue;

// fetch queue - holds freshly fetched instructions
std::vector<proc_inst_t> fetch_queue;

// scheduling queue - combined RS + ROB (size = 2 * (k0 + k1 + k2))
struct ScheduleEntry {
    proc_inst_t instruction;
    uint64_t tag;
    bool fired;
    int fu_index;
    int fu_type;
    bool broadcast;  // true if result has been broadcast on CDB
    bool on_result_bus;  // true if already added to result bus queue
    bool src0_ready;
    uint64_t src0_tag;
    bool src1_ready;
    uint64_t src1_tag;
};

std::vector<ScheduleEntry> schedule_queue;
uint64_t next_tag = 1;
uint64_t schedule_queue_size = 0;

// register scoreboard
std::vector<uint64_t> register_tag;  // tag of the producer instruction for each register

std::vector<proc_inst_t> completed_instructions;

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
    
    // initialize functional units
    auto init_fu = [](std::vector<FunctionalUnit>& units, size_t size) {
        units.resize(size);
        for (FunctionalUnit& u : units) { u.busy = false; u.cycles = 0; u.current_tag = 0; }
    };
    init_fu(k0_units, k0);
    init_fu(k1_units, k1);
    init_fu(k2_units, k2);
    
    // initialize state
    register_tag.assign(128, 0);
    schedule_queue_size = 2 * (k0 + k1 + k2);
    
    // clear queues
    fetch_queue.clear();
    dispatch_queue.clear();
    schedule_queue.clear();
    result_bus_queue.clear();
    retired_queue.clear();
    completed_instructions.clear();
    
    // reset statistics
    total_dispatch_size = 0;
    total_inst_fired = 0;
    total_inst_retired = 0;
    cycle_count = 0;
    max_dispatch_size = 0;
    next_tag = 1;
    fetch_complete = false;
}

// FETCH: fetch up to fetch_width instructions and add to fetch queue
void fetch_phase()
{
    if (fetch_complete) return;
    
    for (uint64_t i = 0; i < fetch_width; i++) {
        proc_inst_t inst;
        if (read_instruction(&inst)) {
            inst.inst_num = next_tag++;  // Use tag for instruction number
            inst.fetch_cycle = cycle_count;  // instruction fetched in current cycle
            inst.disp_cycle = 0; // initialize all other cycles to 0
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
    for (proc_inst_t& inst : fetch_queue) {
        inst.disp_cycle = cycle_count;
        dispatch_queue.push_back(inst);
    }
    fetch_queue.clear();
}

// SCHEDULE: move instructions from dispatch queue to schedule queue
void schedule_phase()
{
    if (schedule_queue.size() >= schedule_queue_size) return;
    
    std::vector<proc_inst_t>::iterator it = dispatch_queue.begin();
    while (it != dispatch_queue.end() && schedule_queue.size() < schedule_queue_size) {
        ScheduleEntry entry;
        entry.instruction = *it;
        entry.instruction.sched_cycle = cycle_count;
        entry.tag = it->inst_num;
        entry.fired = false;
        entry.broadcast = false;
        entry.on_result_bus = false;
        entry.fu_index = -1;
        entry.fu_type = -1;
        
        // determine source readiness based on register tags
        for (int i = 0; i < 2; i++) {
            int src_reg = it->src_reg[i];
            bool src_ready = (src_reg == -1) || (register_tag[src_reg] == 0);
            uint64_t src_tag = (src_ready || src_reg == -1) ? 0 : register_tag[src_reg];
            
            if (i == 0) {
                entry.src0_ready = src_ready;
                entry.src0_tag = src_tag;
            } else {
                entry.src1_ready = src_ready;
                entry.src1_tag = src_tag;
            }
        }
        
        // update register file
        if (it->dest_reg != -1) {
            register_tag[it->dest_reg] = entry.tag;
        }
        
        schedule_queue.push_back(entry);
        it = dispatch_queue.erase(it);
    }
}

void execute_phase()
{
    // helper to get FU array based on opcode
    auto get_fu_units = [](int op_code, int& fu_type) -> std::vector<FunctionalUnit>* {
        if (op_code == 0) { fu_type = 0; return &k0_units; }
        if (op_code == -1 || op_code == 1) { fu_type = 1; return &k1_units; }
        if (op_code == 2) { fu_type = 2; return &k2_units; }
        return nullptr;
    };
    
    // iterate through all instructions in schedule queue looking for ready ones
    for (ScheduleEntry& entry : schedule_queue) {
        if (entry.fired || !entry.src0_ready || !entry.src1_ready) continue;
        
        int fu_type = -1;
        std::vector<FunctionalUnit>* units = get_fu_units(entry.instruction.op_code, fu_type);
        
        if (units) {
            for (size_t j = 0; j < units->size(); j++) {
                if (!(*units)[j].busy) {
                    // found free fu - fire instruction into it
                    entry.fired = true;
                    entry.fu_index = j;
                    entry.fu_type = fu_type;
                    entry.instruction.exec_cycle = cycle_count;
                    
                    (*units)[j].busy = true;
                    (*units)[j].cycles = 1;
                    (*units)[j].current_tag = entry.tag;
                    
                    total_inst_fired++;
                    break;  // only fire to one fu, move to next instruction
                }
            }
        }
    }
}

// STATE UPDATE: decrement FU cycles and push completed instructions to result bus queue
void state_update_phase()
{
    // helper to get FU array based on fu_type
    auto get_fu_array = [](int fu_type) -> std::vector<FunctionalUnit>* {
        if (fu_type == 0) return &k0_units;
        if (fu_type == 1) return &k1_units;
        if (fu_type == 2) return &k2_units;
        return nullptr;
    };
    
    // RETIRE PHASE 1: remove retired instructions from schedule queue
    retired_queue.erase(std::remove_if(retired_queue.begin(), retired_queue.end(), [](uint64_t retired_tag) {
            std::vector<ScheduleEntry>::iterator it = std::find_if(schedule_queue.begin(), schedule_queue.end(), [retired_tag](const ScheduleEntry& e) { return e.tag == retired_tag; });
            if (it != schedule_queue.end()) {
                schedule_queue.erase(it);
                return true;  // remove from retired_queue
            }
            return false;  // keep in retired_queue
        }),
        retired_queue.end()
    );
    
    // RETIRE PHASE 2: Process up to R instructions from result bus
    size_t num_to_retire = std::min<size_t>(num_result_buses, result_bus_queue.size());
    for (size_t i = 0; i < num_to_retire; i++) {
        uint64_t tag = result_bus_queue.front();
        result_bus_queue.pop_front();
        
        std::vector<ScheduleEntry>::iterator it = std::find_if(schedule_queue.begin(), schedule_queue.end(), [tag](const ScheduleEntry& e) { return e.tag == tag; });
        if (it == schedule_queue.end())
            continue;
        
        it->broadcast = true;
        total_inst_retired++;
        completed_instructions.push_back(it->instruction);
        retired_queue.push_back(tag);
        
        // update register file and wakeup dependencies
        if (it->instruction.dest_reg != -1 && register_tag[it->instruction.dest_reg] == tag) {
            register_tag[it->instruction.dest_reg] = 0;
        }
        
        for (ScheduleEntry& rs : schedule_queue) {
            if (!rs.src0_ready && rs.src0_tag == tag) { rs.src0_ready = true; rs.src0_tag = 0; }
            if (!rs.src1_ready && rs.src1_tag == tag) { rs.src1_ready = true; rs.src1_tag = 0; }
        }
    }
    
    // decrement FU cycles
    auto decrement_fu = [](std::vector<FunctionalUnit>& units) {
        for (FunctionalUnit& u : units) if (u.busy && u.cycles > 0) u.cycles--;
    };
    decrement_fu(k0_units);
    decrement_fu(k1_units);
    decrement_fu(k2_units);
    
    // sort schedule_queue by tag
    std::sort(schedule_queue.begin(), schedule_queue.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.tag < b.tag; });
    
    // collect newly completed instructions
    std::vector<uint64_t> newly_completed_tags;
    for (ScheduleEntry& entry : schedule_queue) {
        if (entry.fired && !entry.broadcast && !entry.on_result_bus) {
            std::vector<FunctionalUnit>* units = get_fu_array(entry.fu_type);
            if (units && entry.fu_index >= 0 && entry.fu_index < (int)units->size()) {
                FunctionalUnit& fu = (*units)[entry.fu_index];
                if (fu.busy && fu.current_tag == entry.tag && fu.cycles == 0) {
                    newly_completed_tags.push_back(entry.tag);
                    entry.on_result_bus = true;
                }
            }
        }
    }
    
    std::sort(newly_completed_tags.begin(), newly_completed_tags.end());
    for (uint64_t tag : newly_completed_tags) {
        result_bus_queue.push_back(tag);
    }
    
    // assign state cycle and free functional units for first R instructions on result bus
    auto get_fu_array2 = [](int fu_type) -> std::vector<FunctionalUnit>* {
        if (fu_type == 0) return &k0_units;
        if (fu_type == 1) return &k1_units;
        if (fu_type == 2) return &k2_units;
        return nullptr;
    };
    
    size_t num_to_state = std::min<size_t>(result_bus_queue.size(), num_result_buses);
    for (size_t i = 0; i < num_to_state; i++) {
        uint64_t tag = result_bus_queue[i];
        // find instruction in schedule queue that hasn't been assigned state yet
        std::vector<ScheduleEntry>::iterator it = std::find_if(schedule_queue.begin(), schedule_queue.end(), [tag](const ScheduleEntry& e) { return e.tag == tag && e.instruction.state_cycle == 0; });
        
        if (it != schedule_queue.end()) {
            it->instruction.state_cycle = cycle_count;
            
            // free the functional unit that was executing this instruction
            std::vector<FunctionalUnit>* units = get_fu_array2(it->fu_type);
            if (units && it->fu_index >= 0 && it->fu_index < (int)units->size()) {
                (*units)[it->fu_index].busy = false;
                (*units)[it->fu_index].cycles = 0;
            }
        }
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
        
        state_update_phase();
        execute_phase();
        schedule_phase();
        dispatch_phase();
        fetch_phase();
        
        total_dispatch_size += dispatch_queue.size();
        if (dispatch_queue.size() > max_dispatch_size) {
            max_dispatch_size = dispatch_queue.size();
        }
        
        // check if all instructions are done
        if (fetch_complete && fetch_queue.empty() && dispatch_queue.empty() && 
            schedule_queue.empty() && result_bus_queue.empty()) {
            break;
        }
        
        // safety check for deadlock
        if (cycle_count > 200000) {
            fprintf(stderr, "ERROR: Exceeded 200k cycles - likely deadlock!\n");
            break;
        }
    }
    
    p_stats->cycle_count = cycle_count - 2;
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
    p_stats->avg_inst_retired = (float)total_inst_retired / (float)p_stats->cycle_count;
    p_stats->avg_inst_fired = (float)total_inst_fired / (float)p_stats->cycle_count;
    // p_stats->avg_disp_size = (float)total_dispatch_size / (float)p_stats->cycle_count;
    p_stats->avg_disp_size = static_cast<double>(total_dispatch_size) / (float)p_stats->cycle_count;
    p_stats->max_disp_size = max_dispatch_size;
    p_stats->retired_instruction = total_inst_retired;
}

// ============== DEBUG: REMOVE BEFORE SUBMISSION ==============
void print_instruction_trace()
{
    // sort by instruction number (tag)
    std::sort(completed_instructions.begin(), completed_instructions.end(), [](const proc_inst_t& a, const proc_inst_t& b) {
        return a.inst_num < b.inst_num;
    });
    
    printf("INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");
    for (const proc_inst_t& inst : completed_instructions) {
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
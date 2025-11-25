#include "procsim.hpp"
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
    
    k0_units.resize(k0);
    k1_units.resize(k1);
    k2_units.resize(k2);
    for (auto& unit : k0_units) { unit.busy = false; unit.cycles = 0; unit.current_tag = 0; }
    for (auto& unit : k1_units) { unit.busy = false; unit.cycles = 0; unit.current_tag = 0; }
    for (auto& unit : k2_units) { unit.busy = false; unit.cycles = 0; unit.current_tag = 0; }
    
    register_tag.resize(128, 0);
    fetch_queue.clear();
    dispatch_queue.clear();
    schedule_queue_size = 2 * (k0 + k1 + k2);
    schedule_queue.clear();
    result_bus_queue.clear();
    retired_queue.clear();
    
    total_dispatch_size = 0;
    total_inst_fired = 0;
    total_inst_retired = 0;
    cycle_count = 0;
    max_dispatch_size = 0;
    next_tag = 1;
    fetch_complete = false;
    completed_instructions.clear();
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
    // early exit if schedule queue is full
    if (schedule_queue.size() >= schedule_queue_size) {
        return;
    }
    
    // move instructions from dispatch queue to schedule queue (if space available)
    auto it = dispatch_queue.begin();
    while (it != dispatch_queue.end() && schedule_queue.size() < schedule_queue_size) {
        proc_inst_t inst = *it;
        
        ScheduleEntry entry;
        entry.instruction = inst;
        entry.instruction.sched_cycle = cycle_count;  // instruction scheduled in current cycle
        entry.tag = inst.inst_num;  // Use inst_num as tag
        entry.fired = false;
        entry.broadcast = false;
        entry.on_result_bus = false;
        entry.fu_index = -1;
        entry.fu_type = -1;
        
        // determine source readiness based on register tags
        for (int i = 0; i < 2; i++) {
            bool* ready_ptr = (i == 0) ? &entry.src0_ready : &entry.src1_ready;
            uint64_t* tag_ptr = (i == 0) ? &entry.src0_tag : &entry.src1_tag;
            
            if (inst.src_reg[i] == -1) {
                *ready_ptr = true;
                *tag_ptr = 0;
            } else {
                int src = inst.src_reg[i];
                if (register_tag[src] == 0) {
                    *ready_ptr = true;
                    *tag_ptr = 0;
                } else {
                    *ready_ptr = false;
                    *tag_ptr = register_tag[src];
                }
            }
        }
        
        // update register file: mark dest as not ready and assign new tag
        if (inst.dest_reg != -1) {
            register_tag[inst.dest_reg] = entry.tag;
        }
        
        schedule_queue.push_back(entry);
        it = dispatch_queue.erase(it);
    }
}

void execute_phase()
{
    // fire ready instructions into available FUs
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
                    schedule_queue[i].instruction.exec_cycle = cycle_count;
                    
                    (*units)[j].busy = true;
                    (*units)[j].cycles = 1;  // latency = 1
                    (*units)[j].current_tag = schedule_queue[i].tag;
                    
                    total_inst_fired++;
                    break;
                }
            }
        }
    }
}

// STATE UPDATE: decrement FU cycles and push completed instructions to result bus queue
void state_update_phase()
{
    // decrement cycles for all busy FUs
    for (auto& unit : k0_units) if (unit.busy && unit.cycles > 0) unit.cycles--;
    for (auto& unit : k1_units) if (unit.busy && unit.cycles > 0) unit.cycles--;
    for (auto& unit : k2_units) if (unit.busy && unit.cycles > 0) unit.cycles--;
    
    // sort schedule_queue by tag
    std::sort(schedule_queue.begin(), schedule_queue.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) {
        return a.tag < b.tag;
    });
    
    // collect newly completed instructions (FU cycles reached 0 this cycle)
    std::vector<uint64_t> newly_completed_tags;
    for (size_t i = 0; i < schedule_queue.size(); i++) {
        ScheduleEntry& entry = schedule_queue[i];
        if (!entry.fired || entry.broadcast || entry.on_result_bus) continue;
        
        // check if this instruction's FU has completed
        std::vector<FunctionalUnit>* units = nullptr;
        if (entry.fu_type == 0) units = &k0_units;
        else if (entry.fu_type == 1) units = &k1_units;
        else if (entry.fu_type == 2) units = &k2_units;
        
        if (units && entry.fu_index >= 0 && entry.fu_index < (int)units->size()) {
            FunctionalUnit& fu = (*units)[entry.fu_index];
            if (fu.busy && fu.current_tag == entry.tag && fu.cycles == 0) {
                newly_completed_tags.push_back(entry.tag);
                entry.on_result_bus = true;
            }
        }
    }
    
    // sort newly completed by tag (they all completed in the same cycle)
    std::sort(newly_completed_tags.begin(), newly_completed_tags.end());
    
    // add to result_bus_queue in order (older completions first, then new ones sorted by tag)
    for (uint64_t tag : newly_completed_tags) {
        result_bus_queue.push_back(tag);
    }
    
    // assign STATE to first R instructions in result_bus AND free their FUs
    uint64_t num_to_state = std::min((uint64_t)result_bus_queue.size(), num_result_buses);
    for (uint64_t i = 0; i < num_to_state; i++) {
        uint64_t tag = result_bus_queue[i];
        
        // find entry and assign STATE
        for (auto& entry : schedule_queue) {
            if (entry.tag == tag && entry.instruction.state_cycle == 0) {
                entry.instruction.state_cycle = cycle_count;
                
                // free the FU
                std::vector<FunctionalUnit>* units = nullptr;
                if (entry.fu_type == 0) units = &k0_units;
                else if (entry.fu_type == 1) units = &k1_units;
                else if (entry.fu_type == 2) units = &k2_units;
                
                if (units && entry.fu_index >= 0 && entry.fu_index < (int)units->size()) {
                    (*units)[entry.fu_index].busy = false;
                    (*units)[entry.fu_index].cycles = 0;
                }
                break;
            }
        }
    }
}

// RETIRE: process result bus queue - update register file, wakeup dependencies, remove from schedule queue
void retire_phase()
{
    // phase 1: Remove instructions that were retired in previous cycle(s)
    for (auto it_retired = retired_queue.begin(); it_retired != retired_queue.end(); ) {
        uint64_t retired_tag = *it_retired;
        bool erased_from_schedule = false;
        
        // find and remove from schedule queue
        for (auto it_sched = schedule_queue.begin(); it_sched != schedule_queue.end(); ++it_sched) {
            if (it_sched->tag == retired_tag) {
                schedule_queue.erase(it_sched);
                erased_from_schedule = true;
                break;
            }
        }
        
        // if successfully removed from schedule queue, remove from retired queue
        if (erased_from_schedule) {
            it_retired = retired_queue.erase(it_retired);
        } else {
            ++it_retired;
        }
    }
    
    // Phase 2: Process up to R instructions from result bus queue
    size_t num_to_retire = std::min<size_t>(num_result_buses, result_bus_queue.size());
    
    for (size_t i = 0; i < num_to_retire; i++) {
        uint64_t tag = result_bus_queue.front();
        result_bus_queue.pop_front();
        
        // find the entry with this tag
        ScheduleEntry* entry_ptr = nullptr;
        for (auto& entry : schedule_queue) {
            if (entry.tag == tag) {
                entry_ptr = &entry;
                break;
            }
        }
        
        if (!entry_ptr) continue;
        ScheduleEntry& entry = *entry_ptr;
        
        entry.broadcast = true;  // mark as broadcast/retired
        total_inst_retired++;
        
        // record for debug output
        completed_instructions.push_back(entry.instruction);
        
        // add to retired queue for removal in next cycle
        retired_queue.push_back(tag);
        
        // update register file
        int dest = entry.instruction.dest_reg;
        if (dest != -1 && register_tag[dest] == entry.tag) {
            register_tag[dest] = 0;
        }
        
        // wakeup dependent instructions in schedule queue
        for (auto& rs : schedule_queue) {
            for (int j = 0; j < 2; j++) {
                bool* ready_ptr = (j == 0) ? &rs.src0_ready : &rs.src1_ready;
                uint64_t* tag_ptr = (j == 0) ? &rs.src0_tag : &rs.src1_tag;
                
                if (!(*ready_ptr) && (*tag_ptr) == entry.tag) {
                    *ready_ptr = true;
                    *tag_ptr = 0;
                }
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
        
        retire_phase(); // step 1: retire - process result bus from previous cycle
        state_update_phase(); // step 2: state update - decrement FU cycles, push completed to result bus
        execute_phase(); // step 3: execute - fire ready instructions
        schedule_phase(); // step 4: schedule - move from dispatch queue to schedule queue
        dispatch_phase(); // step 5: dispatch - move from fetch queue to dispatch queue
        fetch_phase(); // step 6: fetch - fetch new instructions
        
        total_dispatch_size += dispatch_queue.size();
        if (dispatch_queue.size() > max_dispatch_size)
            max_dispatch_size = dispatch_queue.size();
        
        // all instructions are done!
        if (fetch_complete && fetch_queue.empty() && dispatch_queue.empty() && schedule_queue.empty() && result_bus_queue.empty())
            break;
        
        // REMOVE LATER
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
    
    // set the final cycle count
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
    p_stats->avg_disp_size = (float)total_dispatch_size / (float)p_stats->cycle_count;
    p_stats->max_disp_size = max_dispatch_size;
    p_stats->retired_instruction = total_inst_retired;
}

// ============== DEBUG: REMOVE BEFORE SUBMISSION ==============
void print_instruction_trace()
{
    // sort by instruction number (tag)
    std::sort(completed_instructions.begin(), completed_instructions.end(),
              [](const proc_inst_t& a, const proc_inst_t& b) {
                  return a.inst_num < b.inst_num;
              });
    
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
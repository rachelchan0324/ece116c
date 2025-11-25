#include "procsim.hpp"

uint64_t R, K0, K1, K2, F;
uint32_t tag_counter;
uint32_t current_cycle;
uint32_t RS_size;

bool no_instructions = false;

std::deque<proc_inst_t> fetch_buffer;
std::deque<proc_inst_t> dispatch_queue; 
std::deque<reservation> reservation_station;
std::deque<reservation> result_bus;
std::deque<reservation> reservation_retired;

bool reg_ready[128];
int reg_tag[128];

std::vector<FU> FU0;
std::vector<FU> FU1;
std::vector<FU> FU2;

uint64_t total_inst_retired   = 0;
uint64_t total_inst_fired     = 0;
uint64_t total_disp_size      = 0;
uint64_t max_disp_queue_size  = 0;

bool debug_mode = true;
std::vector<DebugInfo> debug_log;

void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f) 
{
    R = r;
    K0 = k0;
    K1 = k1;
    K2 = k2;
    F = f;

    tag_counter = 1;
    current_cycle = 0;
    RS_size = 2 * (K0 + K1 + K2);

    for (int i = 0; i < 128; i++) {
        reg_ready[i] = true;
        reg_tag[i] = 0;
    }

    FU0.assign(K0, FU());
    FU1.assign(K1, FU());
    FU2.assign(K2, FU());

    total_inst_retired  = 0;
    total_inst_fired    = 0;
    total_disp_size     = 0;
    max_disp_queue_size = 0;

    fetch_buffer.clear();
    dispatch_queue.clear();
    reservation_station.clear();
}

void record_debug(const proc_inst_t &inst) {
    if (!debug_mode) return;
    debug_log.push_back({
        inst.tag,
        inst.fetch_cycle_num,
        inst.dispatch_cycle_num,
        inst.schedule_cycle_num,
        inst.execute_cycle_num,
        inst.state_cycle_num
    });
}

void fetch()
{
    uint64_t fetch_count = 0;
    while (fetch_count < F) {
        proc_inst_t inst;
        if (!read_instruction(&inst)) {
            no_instructions = true;
            return;
        }

        inst.tag = tag_counter;
        inst.fetch_cycle_num = current_cycle;
        inst.dispatch_cycle_num = 0;
        inst.schedule_cycle_num = 0;
        inst.execute_cycle_num = 0;
        inst.state_cycle_num = 0;

        fetch_buffer.push_back(inst);
        tag_counter++;
        fetch_count++;
    }
}

void dispatch()
{
    while (!fetch_buffer.empty())
    {
        fetch_buffer.front().dispatch_cycle_num = current_cycle;
        dispatch_queue.push_back(fetch_buffer.front());
        fetch_buffer.pop_front();
    }
}

void schedule()
{
    if (reservation_station.size() >= RS_size) {
        return;
    }

    auto it = dispatch_queue.begin();
    while (it != dispatch_queue.end() && reservation_station.size() < RS_size) {
        proc_inst_t inst = *it;

        reservation rs_entry;
        rs_entry.inst = inst;
        rs_entry.inst.schedule_cycle_num = current_cycle;
        rs_entry.executed = false;
        rs_entry.FU_cycles = nullptr;

        for (int i = 0; i < 2; i++) {
            if (inst.src_reg[i] == -1) {
                rs_entry.src_ready[i] = true;
                rs_entry.src_tag[i] = 0;
            } else {
                int src = inst.src_reg[i];
                if (reg_tag[src] == 0) {
                    rs_entry.src_ready[i] = true;
                    rs_entry.src_tag[i] = 0;
                } else {
                    rs_entry.src_ready[i] = false;
                    rs_entry.src_tag[i] = reg_tag[src];
                }
            }
        }

        if (inst.dest_reg != -1) {
            reg_tag[inst.dest_reg]  = inst.tag;
            reg_ready[inst.dest_reg] = false;
        }

        reservation_station.push_back(rs_entry);
        it = dispatch_queue.erase(it);
    }
}


bool rs_tag_compare(const reservation &a, const reservation &b) {
    return a.inst.tag < b.inst.tag;
}

void execute()
{
    std::sort(reservation_station.begin(), reservation_station.end(), rs_tag_compare);

    for (auto &rs_entry : reservation_station) {
        if (rs_entry.executed || !(rs_entry.src_ready[0] && rs_entry.src_ready[1]))
            continue;

        int opc = rs_entry.inst.op_code;
        if (opc == -1) opc = 1;

        std::vector<FU>* temp_FU = nullptr;
        if (opc == 0)      temp_FU = &FU0;
        else if (opc == 1) temp_FU = &FU1;
        else if (opc == 2) temp_FU = &FU2;

        for (auto &slot : *temp_FU) {
            if (!slot.busy) {
                slot.busy = true;
                slot.cycles = 1;
                slot.current_inst = rs_entry.inst;

                rs_entry.inst.execute_cycle_num = current_cycle;
                rs_entry.FU_cycles = &slot.cycles;
                rs_entry.executed = true;
                total_inst_fired++;
                break;
            }
        }
    }
}

void state_update()
{
    for (auto &slot : FU0) if (slot.busy && slot.cycles > 0) slot.cycles--;
    for (auto &slot : FU1) if (slot.busy && slot.cycles > 0) slot.cycles--;
    for (auto &slot : FU2) if (slot.busy && slot.cycles > 0) slot.cycles--;

    std::sort(reservation_station.begin(), reservation_station.end(), rs_tag_compare);

    for (size_t i = 0; i < reservation_station.size(); i++) {
        reservation &rs = reservation_station[i];
        if (rs.executed && rs.FU_cycles && *(rs.FU_cycles) == 0 && rs.result_pushed == false) {
            result_bus.push_back(rs);
            rs.result_pushed = true;
        }
    }

    for (size_t j = 0; j < result_bus.size() && j < R; j++) {
        reservation &ret = result_bus[j];
        ret.inst.state_cycle_num = current_cycle;

        int opc = ret.inst.op_code;
        if (opc == -1) opc = 1;

        std::vector<FU>* fu = nullptr;
        if (opc == 0)      fu = &FU0;
        else if (opc == 1) fu = &FU1;
        else               fu = &FU2;

        for (auto &slot : *fu) {
            if (slot.busy && slot.current_inst.tag == ret.inst.tag) {
                slot.busy = false;
                slot.cycles = 0;
                break;
            }
        }
    }
}

void retire()
{
    for (auto it_ret = reservation_retired.begin(); it_ret != reservation_retired.end(); )
    {
        bool erased_from_rs = false;
        for (auto it_rs = reservation_station.begin(); it_rs != reservation_station.end(); ++it_rs)
        {
            if (it_rs->inst.tag == it_ret->inst.tag)
            {
                reservation_station.erase(it_rs);
                erased_from_rs = true;
                break;
            }
        }

        if (erased_from_rs)
            it_ret = reservation_retired.erase(it_ret);
        else
            ++it_ret;
    }
    size_t num = std::min<size_t>(R, result_bus.size());

    for (size_t i = 0; i < num; i++) {
        reservation done = result_bus.front();
        result_bus.pop_front();
        total_inst_retired++; 

        
        record_debug(done.inst); 
        reservation_retired.push_back(done);

        int d = done.inst.dest_reg;
        if (d != -1 && reg_tag[d] == (int)done.inst.tag) {
            reg_ready[d] = true;
            reg_tag[d]   = 0;
        }

        for (auto &rs : reservation_station) {
            for (int j = 0; j < 2; j++) {
                if (!rs.src_ready[j] && rs.src_tag[j] == done.inst.tag) {
                    rs.src_ready[j] = true;
                    rs.src_tag[j]   = 0;
                }
            }
        }
    }
}

void run_proc(proc_stats_t* p_stats)
{
    bool done = false;

    while (!done) {
        current_cycle++;

        retire();
        state_update();
        execute();
        schedule();
        dispatch();
        fetch();

        uint64_t dq = dispatch_queue.size();
        total_disp_size += dq;
        if (dq > max_disp_queue_size) max_disp_queue_size = dq;

        if (no_instructions &&
            fetch_buffer.empty() &&
            dispatch_queue.empty() &&
            reservation_station.empty() &&
            result_bus.empty())
        {
            done = true;
        }
    }
    p_stats->cycle_count     = current_cycle - 2;
}

int count = 0;

void print_debug_log() {
    if (!debug_mode) return;

    std::sort(debug_log.begin(), debug_log.end(),
              [](const DebugInfo &a, const DebugInfo &b){
                  return a.tag < b.tag;
              });

    std::cout << "INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n";
    for (auto &d : debug_log) {
        if (count < 100000)
        {
            std::cout << d.tag << "\t"
                  << d.fetch << "\t"
                  << d.disp << "\t"
                  << d.sched << "\t"
                  << d.exec << "\t"
                  << d.state << std::endl;
            count++;
        }
    }
    std::cout << std::endl;
}

void complete_proc(proc_stats_t *p_stats)
{
    if (p_stats->cycle_count == 0) return;

    p_stats->retired_instruction = total_inst_retired;

    p_stats->avg_disp_size = (double) total_disp_size / p_stats->cycle_count;
    p_stats->max_disp_size = max_disp_queue_size;

    p_stats->avg_inst_fired   = (double) total_inst_fired   / p_stats->cycle_count;
    p_stats->avg_inst_retired = (double) total_inst_retired / p_stats->cycle_count;

    print_debug_log();
}
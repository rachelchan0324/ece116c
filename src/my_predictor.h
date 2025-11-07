// my_predictor.h
// Advanced Perceptron-Inspired Hybrid Predictor
// Combines multiple advanced techniques:
// - Multi-component TAGE-inspired predictor with multiple history lengths
// - Statistical corrector
// - Path history
// - Loop predictor

class my_update : public branch_update {
public:
	unsigned int pc;
	// TAGE-like components with different history lengths
	unsigned int t0_index, t1_index, t2_index, t3_index;
	int t0_ctr, t1_ctr, t2_ctr, t3_ctr;
	bool t0_pred, t1_pred, t2_pred, t3_pred;
	int provider;  // Which table provided the prediction
	bool final_pred;
	// Loop predictor
	unsigned int loop_index;
	bool use_loop;
};

class my_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	128         // Very long global history
#define T0_BITS	14              // Base predictor: 16K entries
#define T1_BITS	14              // Tagged table 1: 16K entries (history 8)
#define T2_BITS	13              // Tagged table 2: 8K entries (history 21)
#define T3_BITS	13              // Tagged table 3: 8K entries (history 64)
#define LOOP_BITS	10          // Loop predictor: 1K entries
#define PATH_LENGTH	16          // Path history

	my_update u;
	branch_info bi;
	unsigned long long history;  // Global history (use 64 bits)
	unsigned int path_history;   // Compressed path history
	
	// Base predictor (bimodal)
	char base_tab[1<<T0_BITS];  // Signed counters for better learning
	
	// Tagged tables with different history lengths
	char t1_tab[1<<T1_BITS];    // Short history (8 bits)
	char t2_tab[1<<T2_BITS];    // Medium history (21 bits)
	char t3_tab[1<<T3_BITS];    // Long history (64 bits)
	
	// Tags for validation (partial tags to save space)
	unsigned short t1_tag[1<<T1_BITS];
	unsigned short t2_tag[1<<T2_BITS];
	unsigned short t3_tag[1<<T3_BITS];
	
	// Loop predictor
	unsigned short loop_iter[1<<LOOP_BITS];   // Iteration counter
	unsigned char loop_conf[1<<LOOP_BITS];    // Confidence
	bool loop_dir[1<<LOOP_BITS];              // Direction (always same?)

	my_predictor (void) : history(0), path_history(0) { 
		memset (base_tab, 0, sizeof (base_tab));
		memset (t1_tab, 0, sizeof (t1_tab));
		memset (t2_tab, 0, sizeof (t2_tab));
		memset (t3_tab, 0, sizeof (t3_tab));
		memset (t1_tag, 0, sizeof (t1_tag));
		memset (t2_tag, 0, sizeof (t2_tag));
		memset (t3_tag, 0, sizeof (t3_tag));
		memset (loop_iter, 0, sizeof (loop_iter));
		memset (loop_conf, 0, sizeof (loop_conf));
		memset (loop_dir, 0, sizeof (loop_dir));
	}

	// Helper: compute folded history for different lengths
	unsigned int fold_history(unsigned long long h, int bits, int len) {
		unsigned int folded = 0;
		for (int i = 0; i < len; i += bits) {
			folded ^= (h >> i) & ((1 << bits) - 1);
		}
		return folded;
	}
	
	// Helper: compute tag
	unsigned short compute_tag(unsigned int pc, unsigned long long h, int hlen) {
		return (pc ^ (h >> hlen) ^ (h >> (hlen/2))) & 0xFFFF;
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		u.pc = b.address;
		
		if (b.br_flags & BR_CONDITIONAL) {
			unsigned int pc = b.address >> 2;
			
			// BASE PREDICTOR (always available)
			u.t0_index = pc & ((1<<T0_BITS)-1);
			u.t0_ctr = base_tab[u.t0_index];
			u.t0_pred = (u.t0_ctr >= 0);
			
			// TAGGED TABLE 1 (short history: 8 bits)
			u.t1_index = (pc ^ fold_history(history, T1_BITS, 8)) & ((1<<T1_BITS)-1);
			unsigned short tag1 = compute_tag(pc, history, 8);
			bool t1_hit = (t1_tag[u.t1_index] == tag1);
			u.t1_ctr = t1_tab[u.t1_index];
			u.t1_pred = (u.t1_ctr >= 0);
			
			// TAGGED TABLE 2 (medium history: 21 bits)
			u.t2_index = (pc ^ fold_history(history, T2_BITS, 21)) & ((1<<T2_BITS)-1);
			unsigned short tag2 = compute_tag(pc, history, 21);
			bool t2_hit = (t2_tag[u.t2_index] == tag2);
			u.t2_ctr = t2_tab[u.t2_index];
			u.t2_pred = (u.t2_ctr >= 0);
			
			// TAGGED TABLE 3 (long history: 64 bits)
			u.t3_index = (pc ^ fold_history(history, T3_BITS, 64) ^ (path_history << 1)) & ((1<<T3_BITS)-1);
			unsigned short tag3 = compute_tag(pc, history, 64);
			bool t3_hit = (t3_tag[u.t3_index] == tag3);
			u.t3_ctr = t3_tab[u.t3_index];
			u.t3_pred = (u.t3_ctr >= 0);
			
			// SELECTION: Use longest matching history (TAGE principle)
			u.provider = 0;  // default: base
			bool pred = u.t0_pred;
			
			if (t1_hit) {
				pred = u.t1_pred;
				u.provider = 1;
			}
			if (t2_hit) {
				pred = u.t2_pred;
				u.provider = 2;
			}
			if (t3_hit) {
				pred = u.t3_pred;
				u.provider = 3;
			}
			
			// LOOP PREDICTOR (for highly regular loops)
			u.loop_index = pc & ((1<<LOOP_BITS)-1);
			u.use_loop = (loop_conf[u.loop_index] >= 3);  // High confidence
			if (u.use_loop && loop_iter[u.loop_index] > 0) {
				pred = loop_dir[u.loop_index];
			}
			
			u.final_pred = pred;
			u.direction_prediction(pred);
		} else {
			u.direction_prediction(true);
		}
		u.target_prediction(0);
		return &u;
	}

	void update (branch_update *up, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			my_update *mu = (my_update*)up;
			
			// 1. UPDATE GSHARE
			unsigned char *g = &gshare_tab[mu->gshare_index];
			if (taken) {
				if (*g < 3) (*g)++;
			} else {
				if (*g > 0) (*g)--;
			}
			
			// 2. UPDATE LOCAL PREDICTOR
			unsigned char *l = &local_pred_tab[mu->local_index];
			if (taken) {
				if (*l < 3) (*l)++;
			} else {
				if (*l > 0) (*l)--;
			}
			
			// 3. UPDATE BIMODAL
			unsigned char *b = &bimodal_tab[mu->bimodal_index];
			if (taken) {
				if (*b < 3) (*b)++;
			} else {
				if (*b > 0) (*b)--;
			}
			
			// 4. UPDATE CHOICE (only when gshare and local disagree)
			if (mu->gshare_pred != mu->local_pred) {
				unsigned char *c = &choice_tab[mu->choice_index];
				bool gshare_correct = (mu->gshare_pred == taken);
				if (gshare_correct) {
					if (*c > 0) (*c)--;  // Prefer gshare
				} else {
					if (*c < 3) (*c)++;  // Prefer local
				}
			}
			
			// 5. UPDATE LOCAL HISTORY for this branch
			unsigned short *lh = &local_hist_tab[mu->local_history_index];
			*lh = ((*lh << 1) | taken) & ((1<<10)-1);  // 10-bit history
			
			// 6. UPDATE GLOBAL HISTORY
			history = ((history << 1) | taken) & ((1<<HISTORY_LENGTH)-1);
		}
	}
};

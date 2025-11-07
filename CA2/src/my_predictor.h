// my_predictor.h
// Hybrid Tournament Predictor:
// - Gshare with 18-bit history (global correlation)
// - Local predictor with per-branch history patterns
// - Bimodal predictor (PC-based)
// - Tournament selection with adaptive learning

class my_update : public branch_update {
public:
	unsigned int gshare_index;
	unsigned int local_index;
	unsigned int local_history_index;
	unsigned int bimodal_index;
	unsigned int choice_index;
	bool gshare_pred;
	bool local_pred;
	bool bimodal_pred;
};

class my_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	20          // Global history length (longer = better correlation)
#define GSHARE_BITS	16          // 64K gshare entries
#define LOCAL_HIST_BITS	11          // 2K local history table entries
#define LOCAL_PRED_BITS	11          // 2K local prediction entries
#define BIMODAL_BITS	14          // 16K bimodal entries
#define CHOICE_BITS	14          // 16K choice entries

	my_update u;
	branch_info bi;
	unsigned int history;  // Global history register
	
	// Gshare table (2-bit counters)
	unsigned char gshare_tab[1<<GSHARE_BITS];
	
	// Local history table (per-branch 10-bit histories)
	unsigned short local_hist_tab[1<<LOCAL_HIST_BITS];
	
	// Local prediction table (2-bit counters indexed by local history)
	unsigned char local_pred_tab[1<<LOCAL_PRED_BITS];
	
	// Bimodal predictor (2-bit counters, indexed by PC only)
	unsigned char bimodal_tab[1<<BIMODAL_BITS];
	
	// Choice predictor (2-bit counters, chooses best predictor)
	unsigned char choice_tab[1<<CHOICE_BITS];

	my_predictor (void) : history(0) { 
		memset (gshare_tab, 0, sizeof (gshare_tab));
		memset (local_hist_tab, 0, sizeof (local_hist_tab));
		memset (local_pred_tab, 0, sizeof (local_pred_tab));
		memset (bimodal_tab, 0, sizeof (bimodal_tab));
		memset (choice_tab, 0, sizeof (choice_tab));  // Start neutral
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			// 1. GSHARE PREDICTION (global correlation)
			// Better hash: fold and XOR multiple parts of PC
			unsigned int pc1 = b.address >> 2;
			unsigned int pc2 = b.address >> (GSHARE_BITS + 2);
			u.gshare_index = (history ^ pc1 ^ pc2) & ((1<<GSHARE_BITS)-1);
			u.gshare_pred = (gshare_tab[u.gshare_index] >= 2);
			
			// 2. LOCAL PREDICTION (per-branch patterns)
			// Each branch has its own history table
			u.local_history_index = (b.address >> 2) & ((1<<LOCAL_HIST_BITS)-1);
			unsigned int local_hist = local_hist_tab[u.local_history_index];
			u.local_index = local_hist & ((1<<LOCAL_PRED_BITS)-1);
			u.local_pred = (local_pred_tab[u.local_index] >= 2);
			
			// 3. BIMODAL PREDICTION (simple PC-based)
			u.bimodal_index = (b.address >> 2) & ((1<<BIMODAL_BITS)-1);
			u.bimodal_pred = (bimodal_tab[u.bimodal_index] >= 2);
			
			// 4. META-PREDICTOR SELECTION
			// Mix history and PC for better choice indexing
			u.choice_index = ((history ^ (history >> CHOICE_BITS)) ^ (b.address >> 2)) & ((1<<CHOICE_BITS)-1);
			bool use_gshare = (choice_tab[u.choice_index] < 2);
			
			// Final prediction: tournament between gshare and local
			bool pred = use_gshare ? u.gshare_pred : u.local_pred;
			
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

// my_predictor.h
// Advanced Multi-History Hybrid Predictor
// Inspired by adaptive history length selection for different branch patterns
// Uses multiple specialized predictors with different history lengths

class my_update : public branch_update {
public:
	unsigned int index[4];  // Indices for different predictors
	unsigned int local_index; // which entry in the local prediction table
	unsigned int local_history_index; // which entry in the local history table
	unsigned int choice_index; // which entry in the meta-predictor table
	bool pred[4];  // predictions from each predictor
	bool local_pred; // predication from local predicator
	int predictor_used;
};

class my_predictor : public branch_predictor {
public:
// Multiple history lengths - maximizing capacity
#define HISTORY_LENGTH_LONG   18    // Long correlations
#define HISTORY_LENGTH_MEDIUM 11    // Medium-range correlations
#define HISTORY_LENGTH_SHORT  6     // Short-range correlations
#define HISTORY_LENGTH_MICRO  3     // Very short patterns

#define TABLE_BITS_0  22    // 4M entries for long history
#define TABLE_BITS_1  21    // 2M entries for medium history
#define TABLE_BITS_2  20    // 1M entries for short history
#define TABLE_BITS_3  19    // 512K entries for micro history

#define LOCAL_HIST_BITS  14  // 16K local history entries
#define LOCAL_PRED_BITS  18  // 256K local prediction entries
#define CHOICE_BITS      19  // 512K meta-predictor entries

	my_update u;
	branch_info bi;
	
	// Multiple global histories
	unsigned int history_long;
	unsigned int history_medium;
	unsigned int history_short;
	unsigned int history_micro;
	
	// Prediction tables with different history lengths (3-bit counters)
	unsigned char tab0[1<<TABLE_BITS_0];  // Long history table
	unsigned char tab1[1<<TABLE_BITS_1];  // Medium history table
	unsigned char tab2[1<<TABLE_BITS_2];  // Short history table
	unsigned char tab3[1<<TABLE_BITS_3];  // Micro history table
	
	// Local predictor components
	unsigned short local_hist_tab[1<<LOCAL_HIST_BITS];
	unsigned char local_pred_tab[1<<LOCAL_PRED_BITS];
	
	// Meta-predictor for selecting best predictor
	unsigned char choice_tab[1<<CHOICE_BITS];

	my_predictor (void) : history_long(0), history_medium(0), history_short(0), history_micro(0) { 
		memset (tab0, 2, sizeof (tab0));  // Initialize to weakly not-taken
		memset (tab1, 2, sizeof (tab1));
		memset (tab2, 2, sizeof (tab2));
		memset (tab3, 2, sizeof (tab3));
		memset (local_hist_tab, 0, sizeof (local_hist_tab));
		memset (local_pred_tab, 2, sizeof (local_pred_tab));
		memset (choice_tab, 2, sizeof (choice_tab));  // Start neutral
	}


	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			unsigned int pc = b.address >> 2;
			
			// Simple direct-mapped gshare-style indices
			
			// Predictor 0: Long history
			u.index[0] = ((history_long << (TABLE_BITS_0 - HISTORY_LENGTH_LONG)) ^ pc) & ((1<<TABLE_BITS_0)-1);
			u.pred[0] = (tab0[u.index[0]] >= 4);
			
			// Predictor 1: Medium history
			u.index[1] = ((history_medium << (TABLE_BITS_1 - HISTORY_LENGTH_MEDIUM)) ^ pc) & ((1<<TABLE_BITS_1)-1);
			u.pred[1] = (tab1[u.index[1]] >= 4);
			
			// Predictor 2: Short history
			u.index[2] = ((history_short << (TABLE_BITS_2 - HISTORY_LENGTH_SHORT)) ^ pc) & ((1<<TABLE_BITS_2)-1);
			u.pred[2] = (tab2[u.index[2]] >= 4);
			
			// Predictor 3: Micro history
			u.index[3] = ((history_micro << (TABLE_BITS_3 - HISTORY_LENGTH_MICRO)) ^ pc) & ((1<<TABLE_BITS_3)-1);
			u.pred[3] = (tab3[u.index[3]] >= 4);
			
			// Local predictor
			u.local_history_index = pc & ((1<<LOCAL_HIST_BITS)-1);
			unsigned int local_hist = local_hist_tab[u.local_history_index];
			u.local_index = local_hist & ((1<<LOCAL_PRED_BITS)-1);
			u.local_pred = (local_pred_tab[u.local_index] >= 4);
			
			// Meta-predictor
			u.choice_index = (pc ^ history_long ^ (history_medium << 3)) & ((1<<CHOICE_BITS)-1);
			unsigned char choice_val = choice_tab[u.choice_index];
			
			// Simple selection logic
			bool final_pred;
			if (choice_val <= 1) {
				final_pred = u.pred[0];  // Long
				u.predictor_used = 0;
			} else if (choice_val <= 3) {
				final_pred = u.pred[1];  // Medium
				u.predictor_used = 1;
			} else if (choice_val <= 5) {
				final_pred = u.pred[2];  // Short
				u.predictor_used = 2;
			} else if (choice_val == 6) {
				final_pred = u.pred[3];  // Micro
				u.predictor_used = 3;
			} else {
				final_pred = u.local_pred;  // Local
				u.predictor_used = 4;
			}
			
			u.direction_prediction(final_pred);
		} else {
			u.direction_prediction(true);
		}
		u.target_prediction(0);
		return &u;
	}


	void update (branch_update *up, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			my_update *mu = (my_update*)up;
			
			// Update all predictor tables (3-bit saturating counters: 0-7)
			for (int i = 0; i < 4; i++) {
				unsigned char *t = nullptr;
				switch(i) {
					case 0: t = &tab0[mu->index[i]]; break;
					case 1: t = &tab1[mu->index[i]]; break;
					case 2: t = &tab2[mu->index[i]]; break;
					case 3: t = &tab3[mu->index[i]]; break;
				}
				if (taken) {
					if (*t < 7) (*t)++;
				} else {
					if (*t > 0) (*t)--;
				}
			}
			
			// Update local predictor
			unsigned char *l = &local_pred_tab[mu->local_index];
			if (taken) {
				if (*l < 7) (*l)++;
			} else {
				if (*l > 0) (*l)--;
			}
			
			// Update meta-predictor (choice table)
			// Train it to select the best predictor
			unsigned char *c = &choice_tab[mu->choice_index];
			
			// Check which predictors were correct
			bool pred_correct[5];
			for (int i = 0; i < 4; i++) {
				pred_correct[i] = (mu->pred[i] == taken);
			}
			pred_correct[4] = (mu->local_pred == taken);
			
			bool used_correct = pred_correct[mu->predictor_used];
			
			// Update choice based on performance
			if (!used_correct) {
				// Current predictor was wrong, try to shift to a better one
				if (pred_correct[0]) {
					// Long history was right, shift toward it
					if (*c > 0) (*c)--;
				} else if (pred_correct[1]) {
					// Medium history was right
					if (*c < 2 || *c > 3) {
						*c = (*c < 2) ? (*c + 1) : (*c - 1);
					}
				} else if (pred_correct[2]) {
					// Short history was right
					if (*c < 4 || *c > 5) {
						*c = (*c < 4) ? (*c + 1) : (*c - 1);
					}
				} else if (pred_correct[3]) {
					// Micro history was right
					if (*c != 6) {
						*c = (*c < 6) ? (*c + 1) : (*c - 1);
					}
				} else if (pred_correct[4]) {
					// Local was right, shift toward it
					if (*c < 7) (*c)++;
				}
			} else {
				// Current predictor was correct, reinforce it slightly
				switch(mu->predictor_used) {
					case 0: if (*c > 0) (*c)--; break;
					case 1: 
						if (*c < 2) (*c)++;
						else if (*c > 3) (*c)--;
						break;
					case 2:
						if (*c < 4) (*c)++;
						else if (*c > 5) (*c)--;
						break;
					case 3: 
						if (*c < 6) (*c)++;
						else if (*c > 6) (*c)--;
						break;
					case 4: if (*c < 7) (*c)++; break;
				}
			}
			
			// Update local history for this branch
			unsigned short *lh = &local_hist_tab[mu->local_history_index];
			*lh = ((*lh << 1) | taken) & ((1<<12)-1);
			
			// Update all global histories
			history_long = ((history_long << 1) | taken) & ((1<<HISTORY_LENGTH_LONG)-1);
			history_medium = ((history_medium << 1) | taken) & ((1<<HISTORY_LENGTH_MEDIUM)-1);
			history_short = ((history_short << 1) | taken) & ((1<<HISTORY_LENGTH_SHORT)-1);
			history_micro = ((history_micro << 1) | taken) & ((1<<HISTORY_LENGTH_MICRO)-1);
		}
	}
};

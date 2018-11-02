#define DC_LRU 0
#define DC_MRU 7
#define DC_WAYS 8
#define IC_LRU 0
#define IC_MRU 3
#define IC_WAYS 4

dc_set *d_set = &d_cache[index];
dc_set *i_set = &i_cache[index];
int hit_index;
if (cmd == 0) { // read from data cache
	d_set->accessed = 1; // No first write / display on cache status cmd

	if (dc_hit(d_set, &hit_index)) {
		dc_hit_cntr++;
		LRU_update(d_set, hit_index, 0);
		mesi_t *mesi = &d_set->d_line[hit_index].mesi;
		switch (*mesi) {
			case M: break; // No state change
			case E: // From snooping STD
			case S: *mesi = S; break;
		}
	} else { // Miss / I: 
		dc_miss_cntr++;
		int LRU_index = get_LRU_index(set);
		LRU_update(d_set, LRU_index, 0);
		mesi = &d_set->d_line[LRU_index].mesi;
		if (*mesi == M) // Data modified
			// Print write to L2 message. // Write back
		// Print read from L2 message. // Read new data
		*mesi = E;
	}
}
else if (cmd == 1) { // write to data cache
	if (dc_hit(d_set, &hit_index)) {
		dc_hit_cntr++;
		LRU_update(d_set, hit_index, 0);
		mesi_t *mesi = &d_set->d_line[hit_index].mesi;
		switch (*mesi) {
			case M: break; // No state change
			case E: 
			if (!d_set->accessed) // First write
				*mesi = E; // Write through
			else
				*mesi = M; // Write back
			break;
			case S: *mesi = M; break; // Would send Invalidate command.
		}
	} else { // Miss / I: 
		dc_miss_cntr++;
		int LRU_index = get_LRU_index(d_set);
		LRU_update(d_set, LRU_index, 0);
		mesi = &d_set->d_line[LRU_index].mesi;
		if (*mesi == M) // Data modified
			// Print Write to L2 message. // Write back
		// Print RFO from L2 message. // RFO new data
		*mesi = M;
	}
		
	d_set->accessed = 1; // No first write / display on cache status cmd
} else if (cmd == 2) { // read from instruction cache
	i_set->accessed = 1; // No first write / display on cache status cmd
	
	if (ic_hit(i_set, &hit_index)) {
		ic_hit_cntr++;
		ic_LRU_update(i_set, hit_index);
	} else { // Miss: 
		ic_miss_cntr++;
		int LRU_index = ic_get_LRU_index(i_set);
		ic_LRU_update(i_set, LRU_index);
		// Print read from L2 message. // Read new data
		i_set->i_line.valid = 1; // Valid data in line
	}
} else if (cmd == 3) { // Invalidate from snooping processor
	if (dc_hit(d_set, &hit_index)) {
		if (d_set->d_line[hit_index].mesi == S) {
			d_set->d_line[hit_index].mesi == I;
			LRU_update(d_set, hit_index, 1); // Invalidate line
		}
		else
			// Print error message // Other processor should only send command from S state.
	} // Nothing to do if no way has matching tag
} else if (cmd == 4) { // Data request from L2 (RFO from snooping processor)

	d_set->accessed = 1; // Should I do this for Invalidatate or RFO from snooping?

	if (dc_hit(d_set, &hit_index)) {
		LRU_update(d_set, hit_index, 1); // Invalidating line.
		mesi_t *mesi = &d_set->d_line[hit_index].mesi;
		switch (*mesi) {
			case M: 
				// print write to L2 message // Writeback to L2
			case E:
				*mesi = I;
				break;
			case S:
				// Print error // From S state Invalidate would be sent not RFO.
				break;
		}
	} else { // Miss: 
		// Print error message // Either print error or ignore (doesn't make sense to get RFO if we're in I state)
	}
} else if (cmd == 8) {
	// Clear cache
} else if (cmd == 9) {
	// Print contents and state of the cache
} else {
	// Print error, command out of range.
}
	

// Returns 1 if hit, 0 if miss.
// On hit stores line index into index
int dc_hit(dc_set *set, int *index) {
	int hit = 0;
	// Loop though ways
	for (int i = 0; i < DC_WAYS; i++) {
		if ((set->d_line[index].tag == tag) && (set->d_line[index].mesi != I)) {
			hit = 1; // Hit
			*index = i; // Store index of matching way 
		}
	}
	return hit;
}

// Update LRU bits (data cache)
int LRU_update(dc_set *set, int *index, int inv) {
	// If LRU bits are being updated as a result
	// of an invalidate command set LRU to 000 (Least recently
	// used) otherwise store old value.
	int old_LRU_value = inv ? DC_LRU : set->d_line[index].lru;
	for (int i=0; i < DC_WAYS; i++) {
		// decrement all values larger than old tag
		if (set->d_line[i].lru > old_LRU_value)
			set->d_line[i].lru -= 1;
	}
	set->d_line[index].lru = DC_MRU; // Constant for b111
}

// Get index of cache line to be evicted (data cache)
int get_LRU_index(dc_set *set) {
	for (int i=0; i < DC_WAYS; i++) {
		if (set->d_line[i].lru == DC_LRU)
			return i;
	}
	return -1; // Should never get here.
}


// Returns 1 if hit, 0 if miss.
// On hit stores line index into index
int ic_hit(ic_set *set, int *index) {
	int hit = 0;
	// Loop though ways
	for (int i = 0; i < IC_WAYS; i++) {
		if ((set->i_line[index].tag == tag) && (set->i_line[index].valid != 0)) {
			hit = 1; // Hit
			*index = i; // Store index of matching way 
		}
	}
	return hit;
}

// Update LRU bits (instruction cache)
int ic_LRU_update(ic_set *set, int *index) {
	int old_LRU_value = set->i_line[index].lru;
	for (int i=0; i < IC_WAYS; i++) {
		// decrement all values larger than old tag
		if (set->i_line[i].lru > old_LRU_value)
			set->i_line[i].lru -= 1;
	}
	set->d_line[index].lru = IC_MRU; // Constant for b11
}

// Get index of cache line to be evicted (instruction cache)
int ic_get_LRU_index(ic_set *set) {
	for (int i=0; i < IC_WAYS; i++) {
		if (set->i_line[i].lru == IC_LRU)
			return i;
	}
	return -1; // Should never get here.
}

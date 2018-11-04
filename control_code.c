#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DC_LRU 0
#define DC_MRU 7
#define DC_WAYS 8
#define IC_LRU 0
#define IC_MRU 3
#define IC_WAYS 4
#define NUM_ROWS 512
#define NUM_SETS 16384

#define #PRINTOUT 1

struct dc_line {
	int lru;
	int tag;
	int mesi;
}

struct dc_set {
	struct dc_line d_line[8];
	int accessed;
}

struct ic_line {
	int lru;
	int tag;
	int valid;
}

struct ic_set {
	struct ic_line i_line[4];
}

// Global verbose mode flag
// If set display L2 access messages

int v_mode = 0;

int main(int argc, char **argv) {

	if (argc < 2) {  // File name not given
		printf("Usage: cache_controller in_file_name [-verbose]\n");
		return 0;
	}

	if ((argc == 3) && !strcmp("-verbose"))
		v_mode = 1;

	// output variables
	unsigned int index, tag, cmd, addr;

	// passed in log and vector file names
	char *vec_file_name = argv[1];
	

	// Error check for input arguments.
	FILE *vector_file_ptr = fopen(vec_file_name, "r");

	while (!parse(vector_file_ptr, &index, &tag, &cmd, &addr)) { // While there are entries in vec file
		struct dc_set *d_set = &d_cache[index];
		struct ic_set *i_set = &i_cache[index];
		int hit_index;
		int LRU_index;
		dc_line *d_line;
		ic_line *i_line;

		if (cmd == 0) { // read from data cache
			d_set->accessed = 1; // No first write / display on cache status cmd

			if (dc_hit(d_set, &hit_index)) {
				dc_hit_cntr++;
				LRU_update(d_set, hit_index, 0);
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: break; // No state change
					case E: // From snooping STD
					case S: d_line->mesi = S; break;
				}
			} else { // Miss / I: 
				dc_miss_cntr++;
				int LRU_index = get_LRU_index(set);
				LRU_update(d_set, LRU_index, 0);
				d_line = &d_set->d_line[LRU_index];
				if (d_line->mesi == M) // Victim modified
					write_to_L2(dc_addr(d_line)); // Write back
				read_from_L2(addr); // Read in new data
				d_line->mesi = E;
			}
		}
		else if (cmd == 1) { // write to data cache
			if (dc_hit(d_set, &hit_index)) {
				dc_hit_cntr++;
				LRU_update(d_set, hit_index, 0);
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: break; // No state change
					case E: // Write back policy no L2 write 
					case S: d_line->mesi = M; break; // Would send Invalidate command.
				}
			} else { // Miss / I: 
				dc_miss_cntr++;
				int LRU_index = get_LRU_index(d_set);
				LRU_update(d_set, LRU_index, 0);
				d_line = &d_set->d_line[LRU_index];
				if (d_line->mesi == M) // Data modified
					write_to_L2(dc_addr(d_line)); // Write back
				if (!d_set->accessed) { // First write
					read_from_L2(addr); // Write allocate (Should this also be RFO?)
					write_to_L2(addr); // Write through
					d_line->mesi = E;
				}
				else {
					RFO_from_l2(addr); // RFO new data
					d_line->mesi = M;
				}
			}
				
			d_set->accessed = 1; // No first write / display on cache status cmd
		} else if (cmd == 2) { // read from instruction cache
			i_set->accessed = 1; // No first write / display on cache status cmd
			
			if (ic_hit(i_set, &hit_index)) {
				ic_hit_cntr++;
				ic_LRU_update(i_set, hit_index);
				i_set->i_line[hit_index].valid = 1;
			} else { // Miss: 
				ic_miss_cntr++;
				int LRU_index = ic_get_LRU_index(i_set);
				ic_LRU_update(i_set, LRU_index);
				read_from_L2(addr); // Read new data
				i_set->i_line[LRU_index].valid = 1;
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
			if (dc_hit(d_set, &hit_index)) {
				LRU_update(d_set, hit_index, 1); // Invalidating line.
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: 
						// print write to L2 message // Writeback to L2
					case E:
						d_line->mesi = I;
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
	}	
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

int parse(FILE *fp, int *index, int *tag, int *command, unsigned int * address) {
	if(fscanf(fp, "%u %x", command, address) < 2) {
		printf("EOF reached or bad data.\n");
		return -1; // End of file
	else {
		*index = (*address >> 6) & 0x3fff; // index: bits 19-6
		*tag = (*address >> 20) & 0xfff; // tag: bits 32-20
		return 0; // line sucessfully read
	}
}

void read_from_L2(unsigned int address) {
	if (v_mode)
		printf("Read from L2 %x.\n", address);
}

void return_data_L2(unsigned int address) {
	if (v_mode)
		printf("Return data to L2 %x.\n", address);
}

void write_to_L2(unsigned int address) {
	if (v_mode)
		printf("Write to L2 %x.\n", address);
}

void RFO_from_L2(unsigned int address) {
	if (v_mode)
		printf("Read for Ownership from L2 %x.\n", address);
}

// When evicting a line the entire line is evicted to L2.
// We need to use the index and tag to generate the write
// address.
unsigned int dc_addr(dc_line *d_line) {
	unsigned int temp = 0;
	temp |= d_line->index << 6;
	temp |= d_line->tag << 20;
	return temp;
}

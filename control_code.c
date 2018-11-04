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

#define PRINTOUT 1

typedef enum mesi {I, M, E, S} mesi_t;

typedef struct dc_line {
	int lru;
	int tag;
	mesi_t mesi;
} dc_line;

typedef struct dc_set {
	dc_line d_line[8];
	int accessed; // Accessed flag for cache status & first write
} dc_set;

typedef struct ic_line {
	int lru;
	int tag;
	int valid;
} ic_line;

typedef struct ic_set {
	ic_line i_line[4];
	int accessed; // Acessed flag for cache status command
} ic_set;


// On hit stores line index into index and returns 1
int dc_hit(dc_set *, int, int *);
// Update LRU counters (data cache)
int LRU_update(dc_set *, int, int);
// Update LRU bits (instruction cache)
int ic_LRU_update(ic_set *, int);
// Returns index of cache line to be evicted (data cache)
int get_LRU_index(dc_set *);
// Get index of cache line to be evicted (instruction cache)
int ic_get_LRU_index(ic_set *);
// On hit stores line index into index and returns 1
int ic_hit(ic_set *, int, int *);
// Parse lines of vector file
int parse(FILE *, int *, int *, int *, unsigned int *);


void read_from_L2(unsigned int);
void return_data_L2(unsigned int);
void write_to_L2(unsigned int);
void RFO_from_L2(unsigned int);

unsigned int dc_addr(dc_line *, int);

// Global verbose mode flag
// If set display L2 access messages

int v_mode = 0;

int main(int argc, char **argv) {

	if (argc < 2) {  // File name not given
		printf("Usage: cache_controller in_file_name [-verbose]\n");
		return 0;
	}

	if ((argc == 3) && !strcmp(argv[2], "-verbose"))
		v_mode = 1;

	// output variables
	unsigned int index, tag, cmd, addr;

	// passed in log and vector file names
	char *vec_file_name = argv[1];
	
	// Error check for input arguments.
	FILE *vector_file_ptr = fopen(vec_file_name, "r");

	dc_set d_cache[NUM_SETS];
	ic_set i_cache[NUM_SETS];
	dc_set *d_set;
	ic_set *i_set;

	int dc_read_cntr = 0;
	int dc_write_cntr = 0;
	int dc_hit_cntr = 0;
	int dc_miss_cntr = 0;
	int ic_read_cntr = 0;
	int ic_hit_cntr = 0;
	int ic_miss_cntr = 0;
	float hit_ratio = 0;

	while (!parse(vector_file_ptr, &index, &tag, &cmd, &addr)) { // While there are entries in vec file
		dc_set *d_set = &d_cache[index];
		ic_set *i_set = &i_cache[index];
		int hit_index;
		int LRU_index;
		dc_line *d_line;
		ic_line *i_line;

		if (cmd == 0) { // read from data cache
			d_set->accessed = 1; // No first write / display on cache status cmd
			dc_read_cntr++;
			if (dc_hit(d_set, tag, &hit_index)) {
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
				int LRU_index = get_LRU_index(d_set);
				LRU_update(d_set, LRU_index, 0);
				d_line = &d_set->d_line[LRU_index];
				if (d_line->mesi == M) // Victim modified
					write_to_L2(dc_addr(d_line, index)); // Write back
				read_from_L2(addr); // Read in cache line with new data
				d_line->tag = tag; // Store new tag
				d_line->mesi = E;
			}
		}
		else if (cmd == 1) { // write to data cache
			dc_write_cntr++;
			if (dc_hit(d_set, tag, &hit_index)) {
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
					write_to_L2(dc_addr(d_line, index)); // Write back
				if (!d_set->accessed) { // First write
					read_from_L2(addr); // Write allocate (Should this also be RFO?)
					write_to_L2(addr); // Write through
					d_line->mesi = E;
				}
				else {
					RFO_from_L2(addr); // RFO new data
					d_line->mesi = M;
				}
				d_line->tag = tag; // Store new tag
			}
				
			d_set->accessed = 1; // No first write / display on cache status cmd
		} else if (cmd == 2) { // read from instruction cache
			i_set->accessed = 1; // display on cache status cmd
			ic_read_cntr++;
			if (ic_hit(i_set, tag, &hit_index)) {
				ic_hit_cntr++;
				ic_LRU_update(i_set, hit_index);
				i_set->i_line[hit_index].valid = 1;
			} else { // Miss: 
				ic_miss_cntr++;
				int LRU_index = ic_get_LRU_index(i_set);
				ic_LRU_update(i_set, LRU_index);
				read_from_L2(addr); // Read new data
				i_set->i_line[LRU_index].valid = 1;
				i_set->i_line[LRU_index].tag = tag; // store new tag
			}
		} else if (cmd == 3) { // Invalidate from snooping processor
			if (dc_hit(d_set, tag, &hit_index)) {
				if (d_set->d_line[hit_index].mesi == S) {
					d_set->d_line[hit_index].mesi == I;
					LRU_update(d_set, hit_index, 1); // Invalidate line
					// Tag left as-is, could also zero out
				}
				// else print error message.
				// Other processor should only send command from S state.
			} // Nothing to do if no tag match
		} else if (cmd == 4) { // Data request from L2 (RFO from snooping processor)
			if (dc_hit(d_set, tag, &hit_index)) {
				LRU_update(d_set, hit_index, 1); // Invalidating line.
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: 
						write_to_L2(addr); // Writeback to L2 (matching addr so don't need to generate)
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
	// End of vector file. Print status:
	printf("Data Cache Statistics:\n");
	printf("Number of cache reads: %d.\n", dc_read_cntr);
	printf("Number of cache writes: %d.\n", dc_write_cntr);
	printf("Number of cache hits: %d.\n", dc_hit_cntr);
	printf("Nunber of cache misses: %d.\n", dc_miss_cntr);
	hit_ratio = ((dc_read_cntr + dc_write_cntr) == 0) ? 0 : dc_hit_cntr / (float)(dc_read_cntr + dc_write_cntr);
	printf("Cache hit ratio: %f.\n", hit_ratio);


	printf("Instruction Cache Statistics:\n");
	printf("Number of cache reads: %d.\n", ic_read_cntr);
	printf("Number of cache hits: %d.\n", ic_hit_cntr);
	printf("Nunber of cache misses: %d.\n", ic_miss_cntr);
	hit_ratio = (ic_read_cntr == 0) ? 0 : ic_hit_cntr / (float) ic_read_cntr;
	printf("Cache hit ratio: %f.\n", hit_ratio);
}

// Returns 1 if hit, 0 if miss.
// On hit stores line index into index
int dc_hit(dc_set *set, int tag, int *index) {
	int hit = 0;
	// Loop though ways
	for (int i = 0; i < DC_WAYS; i++) {
		if ((set->d_line[i].tag == tag) && (set->d_line[i].mesi != I)) {
			hit = 1; // Hit
			*index = i; // Store index of matching way 
		}
	}
	return hit;
}

// Update LRU bits (data cache)
int LRU_update(dc_set *set, int index, int inv) {
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
int ic_hit(ic_set *set, int tag, int *index) {
	int hit = 0;
	// Loop though ways
	for (int i = 0; i < IC_WAYS; i++) {
		if ((set->i_line[i].tag == tag) && (set->i_line[i].valid != 0)) {
			hit = 1; // Hit
			*index = i; // Store index of matching way 
		}
	}
	return hit;
}

// Update LRU bits (instruction cache)
int ic_LRU_update(ic_set *set, int index) {
	int old_LRU_value = set->i_line[index].lru;
	for (int i=0; i < IC_WAYS; i++) {
		// decrement all values larger than old tag
		if (set->i_line[i].lru > old_LRU_value)
			set->i_line[i].lru -= 1;
	}
	set->i_line[index].lru = IC_MRU; // Constant for b11
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
	} else {
		*index = (*address >> 6) & 0x3fff; // index: bits 19-6
		*tag = (*address >> 20) & 0xfff; // tag: bits 32-20
		return 0; // line sucessfully read
	}
}


// I believe all L2 cache reads and writes are in cache line sized chunks
// and alligned. Thus the lower 6 offset bits should be masked off.
void read_from_L2(unsigned int address) {
	if (v_mode)
		printf("Read from L2 %x.\n", address & ~0x3f);
}

void return_data_L2(unsigned int address) {
	if (v_mode)
		printf("Return data to L2 %x.\n", address & ~0x3f);
}

void write_to_L2(unsigned int address) {
	if (v_mode)
		printf("Write to L2 %x.\n", address & ~0x3f);
}

void RFO_from_L2(unsigned int address) {
	if (v_mode)
		printf("Read for Ownership from L2 %x.\n", address & ~0x3f);
}

// When evicting a line the entire line is evicted to L2.
// We need to use the index and tag to generate the write
// address.
unsigned int dc_addr(dc_line *d_line, int index) {
	unsigned int temp = 0;
	temp |= index << 6;
	temp |= d_line->tag << 20;
	return temp;
}

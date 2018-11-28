// ECE585 Final Project
// cache_controller.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cache_controller.h"

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
	int index, tag, cmd;
	unsigned int addr;

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

	// Zero all values in cache
	d_cache_clear(d_cache);
	i_cache_clear(i_cache);

	while (!parse(vector_file_ptr, &index, &tag, &cmd, &addr)) { // While there are entries in vec file
		dc_set *d_set = &d_cache[index]; // Select set from caches
		ic_set *i_set = &i_cache[index];
		int hit_index; // Index of line with matching tag
		int LRU_index; // Index of line to be evicted
		dc_line *d_line;
		ic_line *i_line;

		if (cmd == 0) { // read from data cache
			dc_read_cntr++;
			if (dc_hit(d_set, tag, &hit_index)) {
				dc_hit_cntr++;
				LRU_update(d_set, hit_index); // Hit, existing data is MRU
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: break; // No state change
					case E: // From snooping STD
					case S: d_line->mesi = S; break;
				}
			} else { // Miss / I: 
				dc_miss_cntr++;
				LRU_index = get_LRU_index(d_set);
				LRU_update(d_set, LRU_index); // Miss, new data will be MRU
				d_line = &d_set->d_line[LRU_index];
				if (d_line->mesi == M) // Victim modified
					write_to_L2(dc_addr(d_line, index)); // Write back
				read_from_L2(addr); // Read in cache line with new data
				d_line->tag = tag; // Store new tag
				d_line->mesi = E;
			}
			d_line->accessed = 1; // Cache line accessed, not cold
			d_set->accessed = 1; // Display on cache status cmd
		}
		else if (cmd == 1) { // write to data cache
			dc_write_cntr++;
			if (dc_hit(d_set, tag, &hit_index)) {
				dc_hit_cntr++;
				LRU_update(d_set, hit_index);
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: break; // No state change
					case E: // Write back policy no L2 write 
					case S: d_line->mesi = M; break; // Would send Invalidate command.
				}
			} else { // Miss / I: 
				dc_miss_cntr++;
				int LRU_index = get_LRU_index(d_set);
				LRU_update(d_set, LRU_index);
				d_line = &d_set->d_line[LRU_index];
				if (d_line->mesi == M) // Data modified
					write_to_L2(dc_addr(d_line, index)); // Write back
				RFO_from_L2(addr); // Miss, must read data
				if (!d_line->accessed) { // First write, write though policy
					write_to_L2(addr);
					d_line->mesi = E;
				}
				else { // Normal write, write back policy
					d_line->mesi = M;
				}
				d_line->tag = tag; // Store new tag
			}
			d_line->accessed = 1; // Cache line written, not cold line.
			d_set->accessed = 1; // Display on cache status cmd
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
					d_set->d_line[hit_index].mesi = I;
					LRU_update(d_set, hit_index);
				}
				else
					printf("Unexpected command: invalidate from non-S state\n");
			} // Nothing to do if miss
		} else if (cmd == 4) { // Data request from L2 (RFO from snooping processor)
			if (dc_hit(d_set, tag, &hit_index)) {
				LRU_update(d_set, hit_index);
				d_line = &d_set->d_line[hit_index];
				switch (d_line->mesi) {
					case M: 
						write_to_L2(addr); // Writeback to L2 (matching addr so don't need to generate)
					case E:
					case S:
						d_line->mesi = I;
						break;
				}
			} // Nothing to do if miss
		} else if (cmd == 8) {
			// Clear cache
			d_cache_clear(d_cache);
			i_cache_clear(i_cache);

			// Reset statistics

			dc_read_cntr = 0;
			dc_write_cntr = 0;
			dc_hit_cntr = 0;
			dc_miss_cntr = 0;
			ic_read_cntr = 0;
			ic_hit_cntr = 0;
			ic_miss_cntr = 0;
			hit_ratio = 0;
		} else if (cmd == 9) {
			// Print contents and state of the cache
			d_cache_status(d_cache);
			i_cache_status(i_cache);
		} else {
			printf("Error: Command out of range. Valid commands 0-4, 8,9\n");
		}
		/* Debug
		// Debug, run d cache status
		 d_cache_status(d_cache);
		// Debug, run i cache status
		i_cache_status(i_cache);
		*/
	}
	// End of vector file. Print status:
	printf("Data Cache Statistics:\n");
	printf("Number of cache reads: %d.\n", dc_read_cntr);
	printf("Number of cache writes: %d.\n", dc_write_cntr);
	printf("Number of cache hits: %d.\n", dc_hit_cntr);
	printf("Number of cache misses: %d.\n", dc_miss_cntr);
	hit_ratio = ((dc_read_cntr + dc_write_cntr) == 0) ? 0 : dc_hit_cntr / (float)(dc_read_cntr + dc_write_cntr);
	printf("Cache hit ratio: %f.\n\n", hit_ratio);


	printf("Instruction Cache Statistics:\n");
	printf("Number of cache reads: %d.\n", ic_read_cntr);
	printf("Number of cache hits: %d.\n", ic_hit_cntr);
	printf("Nunber of cache misses: %d.\n", ic_miss_cntr);
	hit_ratio = (ic_read_cntr == 0) ? 0 : ic_hit_cntr / (float) ic_read_cntr;
	printf("Cache hit ratio: %f.\n\n", hit_ratio);
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
void LRU_update(dc_set *set, int index) {
	// Store old value
	int old_LRU_value = set->d_line[index].lru;
	for (int i=0; i < DC_WAYS; i++) {
		// decrement all values larger than old tag
		if (set->d_line[i].lru > old_LRU_value)
			set->d_line[i].lru -= 1;
	}
	set->d_line[index].lru = DC_MRU; // Constant for b111
}

// Get index of cache line to be evicted (data cache)
int get_LRU_index(dc_set *set) {
	int lru_inv = DC_MRU + 1; // Value to store invalid lru
	int evict_idx = 0; // index of line to be evicted
	
	// Find least recently used
	for (int i=0; i < DC_WAYS; i++) {
		if (set->d_line[i].lru == DC_LRU)
			evict_idx = i;
	}
	// If any invalid lines exist evict the least recently used
	for (int i=0; i < DC_WAYS; i++) {
		if (set->d_line[i].mesi == I) {
			if (set->d_line[i].lru < lru_inv) {
				lru_inv = set->d_line[i].lru;
				evict_idx = i;
			}
		}
	}
	return evict_idx;
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
void ic_LRU_update(ic_set *set, int index) {
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

// Parse a line of the input vector file
int parse(FILE *fp, int *index, int *tag, int *command, unsigned int * address) {
	int read_val = fscanf(fp, "%d", command);
	if (read_val == EOF)
		return -1; // End of file return
		
	if ((*command == 8) || (*command == 9)) {
		fscanf(fp, "%*[^\n]");
		*index = 0;
		*tag = 0;
	} else {
		fscanf(fp, "%x", address);
		*index = (*address >> 6) & 0x3fff; // index: bits 19-6
		*tag = (*address >> 20) & 0xfff; // tag: bits 32-20
	}
	return 0;
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

// Display status of all accessed cache lines
void d_cache_status(dc_set *cache) {
	printf("Data cache status:\n");
	char * state_name[4] = {"I", "M", "E", "S"};
	for (int i = 0; i < NUM_SETS; i++) {
		if (cache[i].accessed == 1) {
			printf("Index 0x%x:\n", i);
			printf("Way   0    1    2    3    4    5    6    7\n");
			printf("LRU ");
			for (int j = 0; j < DC_WAYS; j++)
				printf("  %x  ", cache[i].d_line[j].lru);
			printf("\nMESI");
			for (int j = 0; j < DC_WAYS; j++)
				printf("  %s  ", state_name[cache[i].d_line[j].mesi]);
			printf("\nTag ");
			for (int j = 0; j < DC_WAYS; j++)
				printf(" %x ", cache[i].d_line[j].tag);
			printf("\n");
		}
	}
	printf("\n\n");
}

// Display status of all accessed cache lines
void i_cache_status(ic_set *cache) {
	printf("Instruction cache status:\n");
	char * state_name[2] = {"I", "V"};
	for (int i = 0; i < NUM_SETS; i++) {
		if (cache[i].accessed == 1) {
			printf("Index 0x%x:\n", i);
			printf("Way    0    1    2    3\n");
			printf("LRU  ");
			for (int j = 0; j < IC_WAYS; j++)
				printf("  %x  ", cache[i].i_line[j].lru);
			printf("\nVALID");
			for (int j = 0; j < IC_WAYS; j++)
				printf("  %s  ", state_name[cache[i].i_line[j].valid]);
			printf("\nTag  ");
			for (int j = 0; j < IC_WAYS; j++)
				printf(" %x ", cache[i].i_line[j].tag);
			printf("\n");
		}
	}
	printf("\n\n");
}

// Clear all cache tag array data and accessed flags
void d_cache_clear(dc_set *cache) {
	for (int i = 0; i < NUM_SETS; i++) {
		cache[i].accessed = 0;
		for (int j = 0; j < DC_WAYS; j++) {
			cache[i].d_line[j].lru = 0;
			cache[i].d_line[j].tag = 0;
			cache[i].d_line[j].mesi = I;
		}
	}
}

// Clear all cache tag array data and accessed flags
void i_cache_clear(ic_set *cache) {
	for (int i = 0; i < NUM_SETS; i++) {
		cache[i].accessed = 0;
		for (int j = 0; j < IC_WAYS; j++) {
			cache[i].i_line[j].lru = 0;
			cache[i].i_line[j].tag = 0;
			cache[i].i_line[j].valid = 0;
		}
	}
}

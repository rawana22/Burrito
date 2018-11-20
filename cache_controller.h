// ECE585 Final Project Cache controller
// cache_controller.h

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
	dc_line d_line[DC_WAYS];
	int accessed; // Accessed flag for cache status & first write
} dc_set;

typedef struct ic_line {
	int lru;
	int tag;
	int valid;
} ic_line;

typedef struct ic_set {
	ic_line i_line[IC_WAYS];
	int accessed; // Acessed flag for cache status command
} ic_set;


// On hit stores line index into index and returns 1
int dc_hit(dc_set *, int, int *);
// Update LRU counters (data cache)
void LRU_update(dc_set *, int);
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
void d_cache_status(dc_set *cache);
void i_cache_status(ic_set *cache);

void d_cache_clear(dc_set *);
void i_cache_clear(ic_set *);

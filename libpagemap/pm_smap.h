#ifndef _LIBS_PAGEMAP_PM_SMAP_H
#define _LIBS_PAGEMAP_PM_SMAP_H

#include <pthread.h>
#include <pagemap/pagemap.h>

typedef struct mapinfo mapinfo;
struct mapinfo {
	mapinfo *next;

	// header 
	unsigned long start;
	unsigned long end;
	unsigned long offset;
	unsigned size;
	char perms[5];

	// field
	unsigned rss;
	unsigned pss;
	unsigned shared_clean;
	unsigned shared_dirty;
	unsigned private_clean;
	unsigned private_dirty;
	int pagesize;
	int is_bss;
	int count;
	char name[1];
};

typedef struct HUGEINFO {
	struct HUGEINFO *next;
	int size;
	int refcount;
	char name[1];
}hugeinfo_t;

struct proc_info {
	pid_t pid;
	pm_memusage_t usage;
	hugeinfo_t *hmemhead;
	unsigned long wss;
};

mapinfo *load_maps(int pid, int sort_by_address, int coalesce_by_name);

#endif

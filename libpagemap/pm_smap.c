#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

#include "pm_smap.h"

static int is_hugepage(int size) {
	return !!(size*1024 > getpagesize());
}

static int is_library(const char *name) {
	int len = strlen(name);
	return len >= 4 && name[0] == '/'
		&& name[len-3] == '.'
		&& name[len-2] == 's'
		&& name[len-1] == 'o';
}

// 7f80a7605000-7f80a7606000 r--p 0001e000 ca:01 1701113                    /lib/x86_64-linux-gnu/libselinux.so.1
// 0123456789012345678901234567890123456789012345678901234567890
// 0         1         2         3         4         5

static int parse_header(const char *line, const mapinfo *prev, mapinfo **mi) {
	unsigned long start = 0;
	unsigned long end = 0;
	unsigned long offset = 0;

	char name[128];
	int name_pos;
	char perms[5];

	int is_bss = 0;

	if (sscanf(line, "%lx-%lx %s %lx %*x:%*x %*d%n", &start, &end, &perms, &offset, &name_pos) != 4) {
		*mi = NULL;
		return -1;
	}

	while (isspace(line[name_pos]))
		name_pos += 1;

	if (line[name_pos])
		strncpy(name, line+name_pos, sizeof(name));
	else {
		if (prev && start == prev->end && is_library(prev->name)) {
			strncpy(name, prev->name, sizeof(name));
			is_bss = 1;
		}else {
			strncpy(name, "[anon]", sizeof(name));
		}
	}

	const int name_size = strlen(name) +1;
	struct mapinfo *info = calloc(1, sizeof(mapinfo) + name_size);
	if (!info) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	info->start = start;
	info->end = end;
	info->is_bss = is_bss;
	info->count = 1;
	strncpy(info->name, name, name_size);
	info->offset = offset;
	strncpy(info->perms, perms, 5);

	*mi = info;
	return 0;
}


static int parse_field(mapinfo *mi, const char *line) {
	char field[64];
	int size;

	if (sscanf(line, "%63s %d kB", field, &size) != 2)
		return -1;

	if (!strcmp(field, "Size:")) {
		mi->size = size;
	} else if (!strcmp(field, "Rss:")) {
		mi->rss = size;
	} else if (!strcmp(field, "Pss:")) {
		mi->pss = size;
	} else if (!strcmp(field, "Shared_Clean:")) {
		mi->shared_clean = size;
	} else if (!strcmp(field, "Shared_Dirty:")) {
		mi->shared_dirty = size;
	} else if (!strcmp(field, "Private_Clean:")) {
		mi->private_clean = size;
	} else if (!strcmp(field, "Private_Dirty:")) {
		mi->private_dirty = size;
	} else if (!strcmp(field, "KernelPageSize:")) {
		mi->pagesize = size;
	}

	return 0;
}

static int order_before(const mapinfo *a, const mapinfo *b, int sort_by_address) {
	if (sort_by_address) {
		return a->start < b->start
		|| (a->start == b->start && a->end < b->end);
	}else {
		return strcmp(a->name, b->name) < 0;
	}
}


static void enqueue_map(mapinfo **head, mapinfo *map, int sort_by_address, int coalesce_by_name) {
	mapinfo *prev = NULL;
	mapinfo *current = *head;

	if (!map)
		return;

	for(;;) {
		if (current && coalesce_by_name && !strcmp(map->name, current->name)) {
			current->size += map->size;
			current->rss += map->rss;
			current->pss += map->pss;
			current->shared_clean += map->shared_clean;
			current->shared_dirty += map->shared_dirty;
			current->private_clean += map->private_clean;
			current->private_dirty += map->private_dirty;
			current->is_bss += map->is_bss;
			current->count++;
			free(map);
			break;
		}

		if (!current || order_before(map, current, sort_by_address)) {
			if (prev)
				prev->next = map;
			else
				*head = map;
			map->next = current;
			break;
		}

		prev = current;
		current = current->next;
	}
}


mapinfo *load_maps(int pid, int sort_by_address, int coalesce_by_name) {
	char fn[128];
	FILE *fp;
	char line[1024];
	mapinfo *head = NULL;
	mapinfo *current = NULL;
	int len;

	snprintf(fn, sizeof(fn), "/proc/%d/smaps", pid);
	fp = fopen(fn, "r");
	if (fp == 0) {
		fprintf(stderr, "can not open %s: %s\n", fn, strerror(errno));
		return NULL;
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		len = strlen(line);
		if (line[len - 1] == '\n')
			line[--len] = 0;
		if (current != NULL && !parse_field(current, line))
			continue;

		mapinfo *next;
		if (!parse_header(line, current, &next)) {
			enqueue_map(&head, current, sort_by_address, coalesce_by_name);
			current = next;
			continue;
		}

		fprintf(stderr, "warnning: could not parse map info line: %s\n", line);
	}

	enqueue_map(&head, current, sort_by_address, coalesce_by_name);

	fclose(fp);

	return head;
}

static int verbose = 0;
static int terse = 0;
static int addresses = 0;

static void print_header() {
	if (addresses)
		printf("    start       end ");
	printf("virtual                      shared     shared    private   private\n");
	if (addresses)
		printf("     addr       addr ");
	printf("   size      RSS    PSS      clean      dirty     clean     clean ");
	if (!verbose && !addresses)
		printf("    #     ");
	printf("object\n");
}

static void print_divider() {
	if (addresses)
		printf("-------- ---------- ");
	printf("------------------------------------------------------------------- ");
	if (!verbose && !addresses)
		printf("----- ");
	printf("------------------------------------\n");
}

int pmemshow(int pid) {
	mapinfo *milist;
	mapinfo *mi;

	unsigned long shared_dirty = 0;
	unsigned long shared_clean = 0;
	unsigned long private_dirty = 0;
	unsigned long private_clean = 0;
	unsigned long rss = 0;
	unsigned long pss = 0;
	unsigned long size = 0;
	unsigned long count = 0;
	unsigned long hugepagecnt = 0;

	milist = load_maps(pid, addresses, !verbose && !addresses);
	if (!milist) {
		return 1;
	}

	print_header();
	print_divider();

	for (mi = milist; mi;) {
		mapinfo *last = mi;

		shared_clean += mi->shared_clean;
		shared_dirty += mi->shared_dirty;
		private_clean += mi->private_clean;
		private_dirty += mi->private_dirty;
		rss += mi->rss;
		pss += mi->pss;
		size += mi->size;
		count += mi->count;

		if (terse && !mi->private_dirty)
			goto out;

		if (addresses)
			printf("%08lx %08lx ", mi->start, mi->end);

		printf("%8ld %8ld %8ld %8ld %8ld %8ld %8ld     ", mi->size,
			mi->rss,
			mi->pss,
			mi->shared_clean,
			mi->shared_dirty,
			mi->private_clean,
			mi->private_dirty);
		if (!verbose && !addresses)
			printf("%4ld     ", mi->count);

		printf("%s%s\n", mi->name, mi->is_bss ? " [bss]": "");

out:
		mi = mi->next;
		free(last);
	}

	print_divider();
	print_header();
	print_divider();

	if (addresses)
		printf("                      ");

	printf("%8ld %8ld %8ld %8ld %8ld %8ld %8ld     ", size,
			rss,
			pss,
			shared_clean,
			shared_dirty,
			private_clean,
			private_dirty);

	if (!verbose && !addresses) {
		printf("%4ld     ", count);
	}
	printf("TOTAL\n");
}

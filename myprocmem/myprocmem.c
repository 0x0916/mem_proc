#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* display the help screen */
static void usage(const char *cmd) {
	fprintf(stderr, "Usage: %s [-p pid] [-h] \n"
			"	-p Display memory statistic of special process\n"
			"	-h Display this help info\n", cmd);
}

int main(int argc, char *argv[]) {
	size_t i;
	pid_t pid = 0;
	char *endptr = NULL;
	int ws;

	for (i = 1; i < (size_t)(argc - 1); i++) {
		if (!strcmp(argv[i], "-p")) {
			pid = (pid_t)strtol(argv[argc - 1], &endptr, 10);
			if (*endptr != '\0') {
				fprintf(stderr, "Invalid PID \"%s\".\n", argv[argc - 1]);
				exit(EXIT_FAILURE);
			}
			pmemshow(pid);
			exit(EXIT_FAILURE);
		}
		if (!strcmp(argv[i], "-h")) {
			usage(argv[0]);
			exit(EXIT_SUCCESS);
		}
		fprintf(stderr, "Invalid argument \"%s\".\n", argv[i]);
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_FAILURE);
}

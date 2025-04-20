#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Declaration of server and client runner functions
void run_server(int port, int timeout_seconds, int verbose, FILE *logfile);
void run_client(int port, int verbose, FILE *logfile);

void print_help() {
    printf("Use: ./spaasm [OPTIONS]\n");
    printf("  -h            Displays this help message\n");
    printf("  -s            Start the program in server mode\n");
    printf("  -c            Start the program in client mode\n");
    printf("  -p PORT       Specify the port number to use\n");
    printf("  -t SECONDS    Set client inactivity timeout in seconds (server only)\n");
    printf("  -v            Enable verbose (debug) output to stderr\n");
    printf("  -l FILE       Log actions to the specified log file\n");
}

int main(int argc, char *argv[]) {
    int port = -1;      // Port number to use
    int is_server = 0, is_client = 0;   // Role flags
    int timeout_seconds = 30;   // Default timeout for server inactivity
    int verbose = 0;    // Enable verbose/debug output
    char *log_filename = NULL;  // File name for logging (optional)

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h")) {
            // Print help and exit
            print_help();
            return 0;
        } else if (!strcmp(argv[i], "-s")) {
            is_server = 1;
        } else if (!strcmp(argv[i], "-c")) {
            is_client = 1;
        } else if (!strcmp(argv[i], "-p")) {
            // Parse port number
            if (i + 1 < argc) {
                port = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Missing port for -p\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            // Parse inactivity timeout for server
            timeout_seconds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            // Enable verbose output
            verbose = 1;
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            // Parse log filename
            log_filename = argv[++i];
        }
    }

    // Ensure a valid port is specified
    if (port == -1) {
        fprintf(stderr, "Port not specified (-p)\n");
        return 1;
    }

    // Open logfile if provided
    FILE *logfile = NULL;
    if (log_filename) {
        logfile = fopen(log_filename, "w");
        if (!logfile) {
            perror("fopen log");
            exit(1);
        }
    }

    // Start client or server mode based on arguments
    if (is_client) {
        run_client(port, verbose, logfile);
    } else if (is_server) {
        run_server(port, timeout_seconds, verbose, logfile);
    } else {
        // Default to server mode if neither -c nor -s specified
        run_server(port, timeout_seconds, verbose, logfile);
    }

    // Close log file if it was opened
    if (logfile) fclose(logfile);

    return 0;
}
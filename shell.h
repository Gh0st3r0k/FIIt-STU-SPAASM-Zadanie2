#ifndef SHELL_H
#define SHELL_H

#include <netinet/in.h> // For struct sockaddr_in
#include <sys/types.h>  // For pid_t

#define MAX_CLIENTS 128 // Maximum number of simultaneous clients supported

// Structure to hold information about a connected client
typedef struct {
    pid_t pid;
    int fd;
    struct sockaddr_in addr;
    int active;
} ClientInfo;

// Shared memory pointer to client connection table
extern ClientInfo *clients;

// Main command dispatcher
int handle_command(const char *cmd, int client_fd, int verbose);

#endif

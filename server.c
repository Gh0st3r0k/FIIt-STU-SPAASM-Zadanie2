#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <time.h>

// Shared memory array for storing client information
ClientInfo *clients;

// Unused, but could be used for stats or limits
int client_count = 0;

// Global flag to indicate if the server should continue running
volatile sig_atomic_t running = 1;

// Signal handler for SIGTERM — stops the server loop
void handle_sigterm(int sig) {
    running = 0;
}



// Starts the server on the specified port and handles client connections
void run_server(int port, int timeout_seconds, int verbose, FILE *logfile) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestr[32];

    // Allocate shared memory for client table
    clients = mmap(NULL, sizeof(ClientInfo) * MAX_CLIENTS,
               PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (clients == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Create new process group (for killpg in halt)
    setpgid(0, 0);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(1);
    }

    // Start listening for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(1);
    }

    if (verbose) fprintf(stderr, "[DEBUG] Server running on port %d, waiting for client...\n", port);
    now = time(NULL);
    t = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
    if (logfile) fprintf(logfile, "[%s] [LOG] Server running on port %d, waiting for client...\n", timestr, port);

    // Handle termination signal
    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Disable SA_RESTART to allow breaking from accept()
    sigaction(SIGTERM, &sa, NULL);


    // <===> Main server loop <===>
    while (running) {
        // Accept new client
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            if (!running && errno == EINTR) break; // interrupted by signal
            perror("accept");
            continue;
        }
    
        if (verbose) fprintf(stderr, "[DEBUG] New client connected!\n");
        now = time(NULL);
        t = localtime(&now);
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
        if (logfile) fprintf(logfile, "[%s] [LOG] New client connected!\n", timestr);
    
        // Find free slot in client table
        int index = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                index = i;
                break;
            }
        }

        // Fill client entry
        if (index >= 0) {
            clients[index].fd = client_fd;
            clients[index].pid = -1; // set later
            clients[index].addr = address;
            clients[index].active = 1;
        }

        // <===> Handle client in child process <===>
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            pid_t my_pid = getpid();
            close(server_fd); // Child does not accept new connections
    
            char buffer[1024] = {0};
            fd_set set;
            struct timeval timeout;

            // <===> Per-client loop with timeout <===>
            while (1) {
                FD_ZERO(&set);
                FD_SET(client_fd, &set);

                timeout.tv_sec = timeout_seconds;
                timeout.tv_usec = 0;

                int activity = select(client_fd + 1, &set, NULL, NULL, &timeout);
                if (activity == -1) {
                    perror("select");
                    break;
                } else if (activity == 0) {
                    // Timeout occurred
                    const char *msg = "You have been disconnected due to inactivity\n";
                    write(client_fd, msg, strlen(msg));
                    break;
                }

                // Read client input
                memset(buffer, 0, sizeof(buffer));
                int bytes = read(client_fd, buffer, sizeof(buffer) - 1);
                if (bytes <= 0) break;
    
                buffer[strcspn(buffer, "\n")] = '\0'; // Remove trailing newline

                if (verbose) fprintf(stderr, "[DEBUG] Command from the client: %s\n", buffer);
                now = time(NULL);
                t = localtime(&now);
                strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
                if (logfile) fprintf(logfile, "[%s] [LOG] Command from the client: %s\n", timestr, buffer);
    
                // Dispatch command
                int result = handle_command(buffer, client_fd, verbose);
                if (result == 1) break; // client requested quit
                if (result == 2) {
                    // Server halt requested
                    sleep(1);
                    exit(0);
                }
            }
    
            if (verbose) fprintf(stderr, "[DEBUG] Klient sa odpojil\n");
            now = time(NULL);
            t = localtime(&now);
            strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
            if (logfile) fprintf(logfile, "[%s] [LOG] Klient sa odpojil\n", timestr);

            // Mark client as inactive
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].pid == my_pid) {
                    clients[i].active = 0;
                    clients[i].pid = -1;
                    clients[i].fd = -1;
                    memset(&clients[i].addr, 0, sizeof(clients[i].addr));
                    break;
                }
            }

            close(client_fd);
            exit(0); // Child exits
        } else {
            // Parent process
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == client_fd && clients[i].pid == -1 && clients[i].active) {
                    clients[i].pid = pid;
                    break;
                }
            }
            close(client_fd); // Parent doesn't handle this client directly
        }
    }

    // Cleanup
    close(server_fd);
    if (verbose) fprintf(stderr, "[DEBUG] Server stopped.\n");
    now = time(NULL);
    t = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
    if (logfile) fprintf(logfile, "[%s] [LOG] Server stopped.\n", timestr);
}
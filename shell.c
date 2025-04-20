#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>

// Handles input redirection (command < file)

// Executes the command with the given file as its standard input,
// captures the output and sends it to the client
void handle_input_redirect(const char *cmd, int client_fd) {
    char *cmd_copy_full = strdup(cmd);
    char *redirect_in = strchr(cmd_copy_full, '<');

    if (!redirect_in) return;

    *redirect_in = '\0';  // split command and filename
    redirect_in++;

    // trim leading spaces
    while (*redirect_in == ' ') redirect_in++;

    // trim trailing spaces and newlines
    char *end = redirect_in + strlen(redirect_in) - 1;
    while (end > redirect_in && (*end == ' ' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    int fd = open(redirect_in, O_RDONLY);
    if (fd < 0) {
        perror("open");
        free(cmd_copy_full);
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        close(fd);
        free(cmd_copy_full);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process

        dup2(fd, STDIN_FILENO);       // Redirect input from file
        dup2(pipefd[1], STDOUT_FILENO); // Output → pipe
        dup2(pipefd[1], STDERR_FILENO);
        close(fd);
        close(pipefd[0]);
        close(pipefd[1]);

        // Parse command into arguments
        char *args[64];
        char *token = strtok(cmd_copy_full, " ");
        int i = 0;
        while (token && i < 63) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        execvp(args[0], args);
        perror("execvp");
        exit(1);
    } else {
        // Parent process

        close(fd);
        close(pipefd[1]);

        // Read output and send to client
        char buffer[1024];
        int bytes;
        while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            write(client_fd, buffer, bytes);
        }

        close(pipefd[0]);
        waitpid(pid, NULL, 0);
    }

    free(cmd_copy_full);
}


// Handles output redirection (command > file)

// Executes the command and writes its output to the given file
void handle_output_redirect(const char *cmd, int client_fd) {
    char *cmd_copy_full = strdup(cmd);
    char *redirect_out = strchr(cmd_copy_full, '>');

    if (!redirect_out) return;

    *redirect_out = '\0';        // trim command
    redirect_out++;         // move to filename

    // trim leading spaces
    while (*redirect_out == ' ') redirect_out++; // убрать пробелы

    // trim trailing spaces/newlines
    char *end = redirect_out + strlen(redirect_out) - 1;
    while (end > redirect_out && (*end == ' ' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    int fd = open(redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        free(cmd_copy_full);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect output to file
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        // Parse arguments
        char *args[64];
        char *token = strtok(cmd_copy_full, " ");
        int i = 0;
        while (token && i < 63) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        execvp(args[0], args);
        perror("execvp");
        exit(1);
    } else {
        close(fd);
        waitpid(pid, NULL, 0);
    }

    free(cmd_copy_full);
}


// Executes a simple command without redirection

// Uses fork-exec model and sends output back to client
void execute_command(const char *cmd, int client_fd) {

    // Handle redirection
    if (strchr(cmd, '>')) {
        handle_output_redirect(cmd, client_fd);
        return;
    }

    if (strchr(cmd, '<')) {
        handle_input_redirect(cmd, client_fd);
        return;
    }    

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect output to pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Split command into arguments
        char *args[64];
        char *cmd_copy = strdup(cmd);
        char *token = strtok(cmd_copy, " ");
        int i = 0;
        while (token != NULL && i < 63) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        if (execvp(args[0], args) == -1) {
            perror("execvp");
            exit(1);
        }
    } else if (pid > 0) {
        // Parent: read child's output and forward to client
        close(pipefd[1]);

        char buffer[1024];
        int bytes;
        while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            write(client_fd, buffer, bytes);   // send output to client
        }

        close(pipefd[0]);
        waitpid(pid, NULL, 0);  // wait for child process to finish
    } else {
        perror("fork");
    }
}


// Handles internal and external commands

// Recognizes internal commands like `help`, `halt`, `quit`, `abort`, `stat`
// and delegates others to the shell
// Returns:
// 0 - continue processing
// 1 - terminate the current connection (quit)
// 2 - stop the server (halt)

int handle_command(const char *cmd, int client_fd, int verbose) {
    // Handle internal commands
    if (strcmp(cmd, "help") == 0) {
        const char *msg =
        "Internal commands:\n"
        "  help                 - shows this help message\n"
        "  quit                 - closes this connection\n"
        "  halt                 - stops the server and all clients\n"
        "  stat                 - lists all active clients\n"
        "  abort <index>        - disconnects a specific client\n"
        "  prompt <field> <val> - change prompt (time, username, devicename, end)\n"
        "\n"
        "Prompt customization examples:\n"
        "  prompt username gh0st\n"
        "  prompt devicename ghostOS\n"
        "  prompt time 1|2\n"
        "  prompt end >|#\n"
        "\n"
        "Special characters supported:\n"
        "  ;   - separate multiple commands\n"
        "  #   - comment (ignored)\n"
        "  >   - redirect stdout to file\n"
        "  <   - redirect stdin from file\n";
        write(client_fd, msg, strlen(msg));
        const char *done_marker = "__END__\n";
        write(client_fd, done_marker, strlen(done_marker));
        return 0;
    }

    if (strcmp(cmd, "quit") == 0) {
        const char *msg = "I'm closing the connection...\n";
        write(client_fd, msg, strlen(msg));
        return 1;
    }

    if (strcmp(cmd, "halt") == 0) {
        const char *msg = "I'm stopping the server...\n";
        write(client_fd, msg, strlen(msg));
        killpg(0, SIGTERM);
        return 2;
    }

    if (strcmp(cmd, "stat") == 0) {
        char line[256];
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].pid > 0) {
                char *ip = inet_ntoa(clients[i].addr.sin_addr);
                snprintf(line, sizeof(line),
                         "#%d | PID: %d | FD: %d | IP: %s\n",
                         i, clients[i].pid, clients[i].fd, ip);
                write(client_fd, line, strlen(line));
            }
        }
        const char *done = "__END__\n";
        write(client_fd, done, strlen(done));
        return 0;
    }

    // Handle abort n
    char cmd_copy[1024];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy));
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *cmd1 = strtok(cmd_copy, " ");
    if (cmd1 && strcmp(cmd1, "abort") == 0) {

        if (verbose) fprintf(stderr, "[DEBUG] abort command received\n");

        char *cmd2 = strtok(NULL, " ");
        if (cmd2) {
            int index = atoi(cmd2);
            if (index >= 0 && index < MAX_CLIENTS && clients[index].active) {
                pid_t victim = clients[index].pid;
                if (victim > 0) {
                    kill(victim, SIGTERM);
                    if (victim == getpid()) {
                        const char *msg = "I'm quitting based on 'abort'\n";
                        write(client_fd, msg, strlen(msg));
                        for (int i = 0; i < MAX_CLIENTS; i++) {
                            if (clients[i].pid == getpid()) {
                                clients[i].active = 0;
                                clients[i].pid = -1;
                                clients[i].fd = -1;
                                memset(&clients[i].addr, 0, sizeof(clients[i].addr));
                                break;
                            }
                        }
                        close(client_fd);
                        exit(0);
                    }
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Command 'abort %d' - client %d has been aborted\n", index, index);
                    write(client_fd, msg, strlen(msg));
                } else {
                    write(client_fd, "Error: The specified PID is not valid\n", 30);
                }
            } else {
                write(client_fd, "Error: Invalid client index\n", 27);
            }
        } else {
            write(client_fd, "Use: abort <index>\n", 20);
        }
    
        const char *done = "__END__\n";
        write(client_fd, done, strlen(done));
        return 0;
    }
    

    // Strip comments (marked with #)
    char *cleaned = strdup(cmd);
    char *comment = strchr(cleaned, '#');
    if (comment) *comment = '\0';

    // Split and execute multiple commands separated by ';'
    char *token = strtok(cleaned, ";");
    while (token != NULL) {
        while (*token == ' ') token++; // skip leading spaces
        if (strlen(token) > 0) {
            execute_command(token, client_fd);
        }
        token = strtok(NULL, ";");
    }

    const char *done_marker = "__END__\n";
    write(client_fd, done_marker, strlen(done_marker));

    free(cleaned);
    return 0;
}
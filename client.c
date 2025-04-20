#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>

// Runs the client, connecting to the server and sending/receiving commands
void run_client(int port, int verbose, FILE *logfile) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestr[32];   // Buffer for timestamp string

    int sock = 0;
    struct sockaddr_in serv_addr;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }


    // Prepare server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    // Log and print connection established
    if (verbose) fprintf(stderr, "[DEBUG] Connected to server on port %d\n", port);
    now = time(NULL);
    t = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
    if (logfile) fprintf(logfile, "[%s] [LOG] Connected to server on port %d\n", timestr, port);

    print_prompt(); // Show initial prompt
    fflush(stdout);

    fd_set fds;
    int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds); // Monitor keyboard input
        FD_SET(sock, &fds);         // Monitor server input

        // Wait for either input
        int activity = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // Handle incoming data from server
        if (FD_ISSET(sock, &fds)) {
            char buf[1024];
            int bytes = read(sock, buf, sizeof(buf) - 1);
            if (bytes <= 0) {
                // Server closed connection
                if (verbose) fprintf(stderr, "[DEBUG] Server disconnected. Exiting.\n");
                now = time(NULL);
                t = localtime(&now);
                strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
                if (logfile) fprintf(logfile, "[%s] [LOG] Server disconnected. Exiting.\n", timestr);
                break;
            }

            buf[bytes] = '\0';

            // Look for end marker to separate outputs
            char *pos = strstr(buf, "__END__\n");
            if (pos) {
                *pos = '\0';
                printf("%s", buf);

                // Log the clean response
                if (logfile) {
                    now = time(NULL);
                    t = localtime(&now);
                    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
                    fprintf(logfile, "[%s] [LOG] Received: %s\n", timestr, buf);
                }

                print_prompt();
            } else {
                printf("%s", buf);

                // Log the raw output
                if (logfile) {
                    now = time(NULL);
                    t = localtime(&now);
                    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", t);
                    fprintf(logfile, "[%s] [LOG] Received: %s\n", timestr, buf);
                }
            }

            fflush(stdout);
        }

        // Handle user input
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char input[1024];
            if (!fgets(input, sizeof(input), stdin)) break;

            // Remove newline character
            input[strcspn(input, "\n")] = '\0';

            // Empty input — reprint prompt
            if (strlen(input) == 0) {
                print_prompt();
                fflush(stdout);
                continue;
            }

            // Handle local "prompt" command (not sent to server)
            if (strncmp(input, "prompt ", 7) == 0) {
                char *arg1 = strtok(input + 7, " ");
                char *arg2 = strtok(NULL, "\n");

                if (!arg1 || !arg2) {
                    printf("Usage: prompt <time|username|devicename|end> <value>\n");
                } else if (strcmp(arg1, "time") == 0) {
                    if (strcmp(arg2, "1") == 0)
                        prompt_show_seconds = 0;
                    else if (strcmp(arg2, "2") == 0)
                        prompt_show_seconds = 1;
                    else
                        printf("Error: Time can only be 1 or 2\n");
                } else if (strcmp(arg1, "username") == 0) {
                    strncpy(prompt_username, arg2, sizeof(prompt_username)-1);
                    prompt_username[sizeof(prompt_username)-1] = '\0';
                } else if (strcmp(arg1, "devicename") == 0) {
                    strncpy(prompt_devicename, arg2, sizeof(prompt_devicename)-1);
                    prompt_devicename[sizeof(prompt_devicename)-1] = '\0';
                } else if (strcmp(arg1, "end") == 0) {
                    if (strlen(arg2) > 0)
                        prompt_symbol = arg2[0];
                    else
                        printf("Error: Please specify at least one character for 'end'\n");
                } else {
                    printf("Error: Unknown prompt parameter\n");
                }

                print_prompt();
                fflush(stdout);
                continue;
            }

            // Send regular input to the server
            send(sock, input, strlen(input), 0);

            // Exit if internal quit/halt command was issued
            if (strcmp(input, "quit") == 0 || strcmp(input, "halt") == 0)
                break;
        }
    }

    // Close socket when done
    close(sock);
}

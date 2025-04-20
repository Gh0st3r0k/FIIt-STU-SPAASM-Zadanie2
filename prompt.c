#include "prompt.h"
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <string.h>

// Whether to show seconds in the time portion of the prompt (0 = no, 1 = yes)
int prompt_show_seconds = 0;

// Prompt end symbol (e.g. '>', '#')
char prompt_symbol = '>';

// Custom username for prompt (overrides system username if set)
char prompt_username[64] = "";

// Custom device name for prompt (overrides hostname if set)
char prompt_devicename[64] = "";

// Prints the current prompt to stdout
void print_prompt() {
    char time_str[32];          // Formatted time string
    char hostname_buf[256];     // Buffer for system hostname
    const char *hostname;       // Pointer to final hostname
    const char *username;       // Pointer to final username

    // Determine which device name to display
    if (*prompt_devicename)
        hostname = prompt_devicename;
    else if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0)
        hostname = hostname_buf;
    else
        hostname = "host"; // fallback

    // Determine which username to display
    if (*prompt_username)
        username = prompt_username;
    else {
        struct passwd *pw = getpwuid(getuid());
        username = pw ? pw->pw_name : "user";
    }

    // Get current time and format
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (prompt_show_seconds)
        strftime(time_str, sizeof(time_str), "%H:%M:%S", t);
    else
        strftime(time_str, sizeof(time_str), "%H:%M", t);

    // Final prompt format: TIME USER@DEVICE SYMBOL
    printf("%s %s@%s %c ", time_str, username, hostname, prompt_symbol);
    fflush(stdout);
}

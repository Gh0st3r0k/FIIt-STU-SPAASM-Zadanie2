#ifndef PROMPT_H
#define PROMPT_H

#include <stdio.h>

// If 1, the prompt will show HH:MM:SS instead of just HH:MM
// Set using prompt time 1|2
extern int prompt_show_seconds;

// Character used at the end of the prompt (e.g. '>', '#')
// Set using prompt end X
extern char prompt_symbol;

// Custom username to be shown in the prompt
// Set using prompt username NAME
extern char prompt_username[64];

// Custom device name (hostname) to be shown in the prompt
// Set using prompt devicename NAME
extern char prompt_devicename[64];

// Prints the formatted prompt based on current settings
void print_prompt(void);

#endif
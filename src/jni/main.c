#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define VERSION "v1.0.3"

/* Long command line options */
static struct option long_options[] = {
    {"user", required_argument, NULL, 'u'},
    {"group", required_argument, NULL, 'g'},
    {"shell", required_argument, NULL, 's'},
    {"login", no_argument, NULL, 'i'},
    {"background", no_argument, NULL, 'b'},
    {"preserve-env", no_argument, NULL, 'E'},
    {"edit", required_argument, NULL, 'e'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0}
};

/**
 * Join multiple command line arguments into a single string
 * 
 * @param count Number of arguments to join
 * @param args Array of argument strings
 * @return Newly allocated string with joined arguments, or NULL on failure
 */
char* join_args(int count, char** args) {
    if (count <= 0) return NULL;
    
    /* Calculate total memory needed including spaces and null terminator */
    size_t total_len = 1;  /* Space for null terminator */
    for (int i = 0; i < count; i++) {
        if (args[i]) {
            total_len += strlen(args[i]) + 1;  /* +1 for space */
        }
    }
    
    char* buffer = malloc(total_len);
    if (!buffer) return NULL;
    
    /* Copy arguments with space separators */
    char* ptr = buffer;
    for (int i = 0; i < count; i++) {
        if (args[i]) {
            size_t len = strlen(args[i]);
            memcpy(ptr, args[i], len);
            ptr += len;
            *ptr++ = ' ';
        }
    }
    
    /* Replace trailing space with null terminator */
    if (ptr > buffer) ptr[-1] = '\0';
    else *ptr = '\0';
    
    return buffer;
}

/**
 * Print program usage information
 */
void print_help() {
    printf("Sudo for Android NDK\n\n");
    printf("Usage: sudo [options] [command]\n");
    printf("Options:\n");
    printf("  -u, --user=USER        run command as specified user (default: 0)\n");
    printf("  -g, --group=GRP        specify primary group\n");
    printf("  -s, --shell=SHELL      use specified shell\n");
    printf("  -i, --login            run login shell\n");
    printf("  -b, --background       run command in background\n");
    printf("  -E, --preserve-env     preserve environment variables\n");
    printf("  -e, --edit=FILE        edit file with nano\n");
    printf("  -h, --help             show this help message\n");
    printf("  -V, --version          display version\n");
}

int main(int argc, char* argv[]) {
    char* target_user = NULL;
    char* primary_group = NULL;
    char* command = NULL;
    char* shell = NULL;
    char* edit_file = NULL;
    int login = 0;
    int preserve_env = 0;
    int background = 0;
    int edit_mode = 0;
    int opt;

    /* Parse command line options */
    while ((opt = getopt_long(argc, argv, "+u:g:s:ibEe:hV", long_options, NULL)) != -1) {
        switch (opt) {
        case 'u': target_user = optarg; break;
        case 'g': primary_group = optarg; break;
        case 's': shell = optarg; break;
        case 'i': login = 1; break;
        case 'b': background = 1; break;
        case 'E': preserve_env = 1; break;
        case 'e': edit_mode = 1; edit_file = optarg; break;
        case 'h': print_help(); return 0;
        case 'V': printf("Sudo for Android NDK %s\n", VERSION); return 0;
        default:  return 1;
        }
    }

    /* Handle edit mode - launch nano to edit specified file */
    if (edit_mode) {
        /* Cannot combine edit mode with other commands */
        if (optind < argc) {
            fprintf(stderr, "Error: Can't combine -e with command\n");
            return 1;
        }
        
        /* Allocate memory for nano command (fixed buffer size) */
        command = malloc(strlen(edit_file) + 7);  /* "nano " (5) + file + space + null */
        if (!command) {
            perror("malloc");
            return 1;
        }
        sprintf(command, "nano %s", edit_file);
        
        /* Default to root user for file editing */
        if (!target_user) target_user = "0";
    }

    /* Handle regular command mode - join all remaining arguments */
    if (!edit_mode && optind < argc) {
        command = join_args(argc - optind, &argv[optind]);
        if (!command) {
            fprintf(stderr, "Error: Failed to build command\n");
            return 1;
        }
    }

    /* Determine if a command is required */
    int need_command = 1;
    if (login && !command && !edit_mode) {
        /* Login shell mode - no command needed */
        need_command = 0;
    }

    /* Show help if no command specified and command is required */
    if (need_command && !command && !edit_mode) {
        print_help();
        /* Clean up resources before exit */
        if (edit_mode && command) free(command);
        return 0;
    }

    /* Build su command arguments array */
    char* su_argv[32] = { "su" };
    int arg_count = 1;
    
    /* Add target user */
    if (target_user) su_argv[arg_count++] = target_user;

    /* Add primary group if specified */
    if (primary_group) {
        su_argv[arg_count++] = "--group";
        su_argv[arg_count++] = primary_group;
    }

    /* Add optional flags */
    if (preserve_env) su_argv[arg_count++] = "--preserve-environment";
    if (login) su_argv[arg_count++] = "-l";
    
    /* Add shell if specified */
    if (shell) {
        su_argv[arg_count++] = "--shell";
        su_argv[arg_count++] = shell;
    }
    
    /* Add command to execute */
    if (command) {
        su_argv[arg_count++] = "-c";
        su_argv[arg_count++] = command;
    }
    su_argv[arg_count] = NULL;  /* Null terminate the argument list */

    /* Execute the su command */
    int ret = 0;
    if (background) {
        /* Run in background mode - fork and detach */
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process: create new session and execute command */
            setsid();  /* Detach from parent terminal */
            execvp("su", su_argv);
            perror("execvp");  /* Only reached if execvp fails */
            exit(1);
        } else if (pid < 0) {
            perror("fork");
            ret = 1;
        }
        /* Parent continues without waiting */
    } else {
        /* Run in foreground mode - replace current process */
        execvp("su", su_argv);
        perror("execvp");  /* Only reached if execvp fails */
        ret = 1;
    }

    /* Clean up allocated memory */
    if (command) free(command);
    
    return ret;
}
#include "swish_funcs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "string_vector.h"

#define MAX_ARGS 10

/*
 * Helper function to run a single command within a pipeline. You should make
 * make use of the provided 'run_command' function here.
 * tokens: String vector containing the tokens representing the command to be
 * executed, possible redirection, and the command's arguments.
 * pipes: An array of pipe file descriptors.
 * n_pipes: Length of the 'pipes' array
 * in_idx: Index of the file descriptor in the array from which the program
 *         should read its input, or -1 if input should not be read from a pipe.
 * out_idx: Index of the file descriptor in the array to which the program
 *          should write its output, or -1 if output should not be written to
 *          a pipe.
 * Returns 0 on success or -1 on error.
 */
int run_piped_command(strvec_t *tokens, int *pipes, int n_pipes, int in_idx, int out_idx) {
    // need to redirect standard input from pipe
    if (in_idx != -1) {
        if (dup2(pipes[in_idx], STDIN_FILENO) == -1) {
            perror("dup2 input failed");
            exit(EXIT_FAILURE);
        }
    }

    // redirecting standard output to pipe
    if (out_idx != -1) {
        if (dup2(pipes[out_idx], STDOUT_FILENO) == -1) {
            perror("dup2 output failed");
            exit(EXIT_FAILURE);
        }
    }

    // I need to close all pipe fds
    for (int i = 0; i < n_pipes; i++) {
        close(pipes[i]);
    }

    // executing run command
    if (run_command(tokens) == -1) {
        perror("run_command failed");
        exit(EXIT_FAILURE);
    }

    // it shouldn't reach here if successfull
    exit(EXIT_FAILURE);
    return -1;
}

int run_pipelined_commands(strvec_t *tokens) {
    // Counting the number of "|" tokens to see how many pipes & commands are needed
    int num_pipe_tokens = strvec_num_occurrences(tokens, "|");
    int num_cmds = num_pipe_tokens + 1;

    // Allocating an array for the pipe file descriptors
    int total_pipe_fds = num_pipe_tokens * 2;
    int *pipefds = NULL;
    if (num_pipe_tokens > 0) {
        pipefds = malloc(sizeof(int) * total_pipe_fds);
        if (pipefds == NULL) {
            perror("malloc for pipefds failed");
            return -1;
        }
        // Creating pipes
        for (int i = 0; i < num_pipe_tokens; i++) {
            if (pipe(pipefds + i * 2) == -1) {
                perror("pipe creation failed");
                /* Cleanup already created pipes */
                for (int j = 0; j < i; j++) {
                    close(pipefds[j * 2]);
                    close(pipefds[j * 2 + 1]);
                }
                free(pipefds);
                return -1;
            }
        }
    }

    // Parsing the tokens into separate commands (each command is a strvec_t)
    strvec_t *commands = malloc(sizeof(strvec_t) * num_cmds);
    if (commands == NULL) {
        perror("malloc for commands failed");
        if (pipefds) {
            free(pipefds);
        }
        return -1;
    }

    int cmd_index = 0;
    int start = 0;
    for (int i = 0; i < tokens->length; i++) {
        if (tokens->data[i][0] == '|' && tokens->data[i][1] == '\0') {
            if (strvec_slice(tokens, &commands[cmd_index], start, i) == -1) {
                perror("strvec_slice failed");
                for (int k = 0; k < cmd_index; k++) {
                    strvec_clear(&commands[k]);
                }
                free(commands);
                if (pipefds) {
                    free(pipefds);
                }
                return -1;
            }
            cmd_index++;
            start = i + 1;
        }
    }
    // Last command, from the last "|" to the end of tokens
    if (strvec_slice(tokens, &commands[cmd_index], start, tokens->length) == -1) {
        perror("strvec_slice failed");
        for (int k = 0; k < cmd_index; k++) {
            strvec_clear(&commands[k]);
        }
        free(commands);
        if (pipefds) {
            free(pipefds);
        }
        return -1;
    }

    // Fork child processes for each command.
    pid_t *child_pids = malloc(sizeof(pid_t) * num_cmds);
    if (child_pids == NULL) {
        perror("malloc for child_pids failed");
        for (int k = 0; k < num_cmds; k++) {
            strvec_clear(&commands[k]);
        }
        free(commands);
        if (pipefds) {
            free(pipefds);
        }
        return -1;
    }

    for (int i = num_cmds - 1; i >= 0; i--) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            // Cleaning up
            if (pipefds) {
                for (int j = 0; j < total_pipe_fds; j++) {
                    close(pipefds[j]);
                }
                free(pipefds);
            }
            for (int k = 0; k < num_cmds; k++) {
                strvec_clear(&commands[k]);
            }
            free(commands);
            free(child_pids);
            return -1;
        } else if (pid == 0) {
            // In the child process, I need to set up the appropriate pipe redirections.
            int in_idx = -1, out_idx = -1;
            if (i != 0) {
                // redirecting STDIN from previous pipe’s read end, Not the first command.
                in_idx = (i - 1) * 2;
            }
            if (i != num_cmds - 1) {
                // Not the last command: redirecting STDOUT to current pipe’s write end.
                out_idx = i * 2 + 1;
            }
            // Calling helper to perform redirection and execute the command.
            run_piped_command(&commands[i], pipefds, total_pipe_fds, in_idx, out_idx);
            exit(EXIT_FAILURE);    // just in case run_piped_command returns
        } else {
            // In the parent, I need to record the child process ID.
            child_pids[i] = pid;
        }
    }

    // I am closing all pipe fds in the parent to avoid leaks.
    if (pipefds) {
        for (int i = 0; i < total_pipe_fds; i++) {
            close(pipefds[i]);
        }
        free(pipefds);
    }

    // Waiting for all child processes to complete
    int status;
    int ret = 0;
    for (int i = 0; i < num_cmds; i++) {
        if (waitpid(child_pids[i], &status, 0) == -1) {
            perror("waitpid failed");
            ret = -1;
        }
    }
    free(child_pids);

    // Cleaning up the memory used by the command token vectors
    for (int i = 0; i < num_cmds; i++) {
        strvec_clear(&commands[i]);
    }
    free(commands);

    return ret;
}

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ALPHABET_LEN 26

/*
 * Counts the number of occurrences of each letter (case insensitive) in a text
 * file and stores the results in an array.
 * file_name: The name of the text file in which to count letter occurrences
 * counts: An array of integers storing the number of occurrences of each letter.
 *     counts[0] is the number of 'a' or 'A' characters, counts [1] is the number
 *     of 'b' or 'B' characters, and so on.
 * Returns 0 on success or -1 on error.
 */
int count_letters(const char *file_name, int *counts) {
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (isalpha(ch)) {
            ch = tolower(ch);
            int index = ch - 'a';
            counts[index]++;
        }
    }

    if (fclose(file) == EOF) {
        perror("fclose");
        return -1;
    }
    return 0;
}

/*
 * Processes a particular file(counting occurrences of each letter)
 *     and writes the results to a file descriptor.
 * This function should be called in child processes.
 * file_name: The name of the file to analyze.
 * out_fd: The file descriptor to which results are written
 * Returns 0 on success or -1 on error
 */
int process_file(const char *file_name, int out_fd) {
    if (file_name == NULL || out_fd < 0) {
        fprintf(stderr, "bad input, try again");
        return -1;
    }

    int counts[ALPHABET_LEN] = {0};

    if (count_letters(file_name, counts) == -1) {
        return -1;
    }

    ssize_t n = write(out_fd, counts, sizeof(counts));
    if (n != sizeof(counts)) {
        perror("pipe write failed");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        // no input files
        return 0;
    }

    int file_count = argc - 1;
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pids[file_count];

    for (int i = 0; i < file_count; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            // child process
            close(pipe_fds[0]);

            if (process_file(argv[i + 1], pipe_fds[1]) == -1) {
                close(pipe_fds[1]);
                exit(1);
            }

            close(pipe_fds[1]);
            exit(0);
        }

        // parent process
        pids[i] = pid;
    }

    close(pipe_fds[1]);

    int total_counts[ALPHABET_LEN] = {0};

    for (int i = 0; i < file_count; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");

        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            int temp[ALPHABET_LEN] = {0};
            ssize_t pipe_read = read(pipe_fds[0], temp, sizeof(temp));

            if (pipe_read == sizeof(temp)) {
                for (int j = 0; j < ALPHABET_LEN; j++) {
                    total_counts[j] += temp[j];
                }
            } else {
                perror("read");
            }
        }
    }

    if (close(pipe_fds[0]) == -1) {
        perror("close pipe read end");
    }

    for (int i = 0; i < ALPHABET_LEN; i++) {
        printf("%c Count: %d\n", 'a' + i, total_counts[i]);
    }

    return 0;
}

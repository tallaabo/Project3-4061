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
    if (!file) {
        perror("fopen");
        return -1;
    }

    int c;
    while ((c = fgetc(file)) != EOF) {
        if (isalpha(c)) {
            counts[tolower(c) - 'a']++;
        }
    }

    fclose(file);
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
    int counts[ALPHABET_LEN] = {0};
    if (count_letters(file_name, counts) == -1)
        return -1;

    if (write(out_fd, counts, sizeof(counts)) != sizeof(counts))
        return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        // No files to consume, return immediately
        return 0;
    }

    int num_files = argc - 1;
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pids[num_files];
    for (int i = 1; i <= num_files; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            close(pipe_fds[0]);
            if (process_file(argv[i], pipe_fds[1]) == -1) {
                // Let count_letters print fopen error, just exit silently
                exit(1);
            }
            exit(0);
        }
        pids[i - 1] = pid;
    }

    close(pipe_fds[1]);

    int total_counts[ALPHABET_LEN] = {0};
    for (int i = 0; i < num_files; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            int temp_counts[ALPHABET_LEN];
            if (read(pipe_fds[0], temp_counts, sizeof(temp_counts)) != sizeof(temp_counts)) {
                perror("read");
                return 1;
            }
            for (int j = 0; j < ALPHABET_LEN; j++) {
                total_counts[j] += temp_counts[j];
            }
        }
    }

    for (int i = 0; i < ALPHABET_LEN; i++) {
        printf("%c Count: %d\n", 'a' + i, total_counts[i]);
    }
    return 0;
}

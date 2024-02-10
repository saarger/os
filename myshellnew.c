#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

int execute_command(int count, char **arglist, int background, int redirection, int pipe_index);

void setup_signals_for_child(void);

void handle_sigchld(int sig);

void handle_background_process(pid_t pid);


int prepare(void) {
    struct sigaction sa;

    // Clear the signal set
    sigemptyset(&sa.sa_mask); // Corrected to use sa.sa_mask

    // Set the handler function
    sa.sa_handler = &handle_sigchld;

    // No flags or use SA_RESTART to automatically restart system calls
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    // Apply the signal action settings for SIGCHLD
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Error - failed to setup SIGCHLD handler");
        return -1;
    }

    // Ignore SIGINT in the parent process, as you have before
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("Error - failed to ignore SIGINT");
        return -1;
    }

    return 0;
}

int process_arglist(int count, char **arglist) {
    int background = 0, redirection = 0, pipe_index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "&") == 0) {
            background = 1;
        } else if (strcmp(arglist[i], ">") == 0) {
            redirection = 1;
        } else if (strcmp(arglist[i], "|") == 0) {
            pipe_index = i;
        }
    }

    return execute_command(count, arglist, background, redirection, pipe_index);
}

int finalize(void) {
    return 0;
}

void handle_background_process(pid_t pid) {
    // Detach the process by not waiting for it
    printf("Started background process PID: %d\n", pid);
}

void handle_sigchld(int sig) {
    // Wait for all children without blocking
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
}

// Simplified signal setup utility
void setup_signals_for_child(void) {
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        perror("Error - failed to set default SIGINT");
        exit(1);
    }
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        perror("Error - failed to set default SIGCHLD");
        exit(1);
    }
}


// Function to execute a single command or a command with output redirection or piping
int execute_command(int count, char **arglist, int background, int redirection, int pipe_index) {
    int pipefd[2];
    pid_t pid_first, pid_second;
    int status;

    // Setup pipe if there's a pipe_index
    if (pipe_index != -1 && pipe(pipefd) == -1) {
        perror("Pipe creation failed");
        return -1;
    }

    if (background) {
        arglist[count - 1] = NULL; // Remove the '&' from the arglist
        count--; // Adjust count to exclude the '&' from the arguments
    }

    pid_first = fork();
    if (pid_first == -1) {
        perror("Fork failed");
        return -1;
    }

    if (pid_first == 0) { // Child process for the first command or the only command if no piping
        if (background) {
            // Ensure the background process does not terminate on SIGINT
            signal(SIGINT, SIG_IGN);
        }

        if (redirection) { // Setup output redirection
            int fd = open(arglist[count - 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
            if (fd == -1) {
                perror("Failed to open file for redirection");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            arglist[count - 2] = NULL; // Adjust the arglist to remove redirection symbols
        }

        if (pipe_index != -1) { // Setup piping for the first command
            close(pipefd[0]); // Close the read end as it's not used by the first command
            dup2(pipefd[1], STDOUT_FILENO); // Redirect STDOUT to the pipe
            close(pipefd[1]);
            arglist[pipe_index] = NULL; // Adjust the arglist to end before the pipe symbol
        }

        // Execute the command
        execvp(arglist[0], arglist);
        perror("Failed to execute command");
        exit(1); // If execvp returns, it failed
    } else { // Parent process
        if (pipe_index != -1) { // Handle the second part of the pipe in a new child process
            pid_second = fork();
            if (pid_second == 0) {
                close(pipefd[1]); // Close the write end as it's not used by the second command
                dup2(pipefd[0], STDIN_FILENO); // Redirect STDIN to the pipe
                close(pipefd[0]);

                // Execute the second command
                execvp(arglist[pipe_index + 1], &arglist[pipe_index + 1]);
                perror("Failed to execute piped command");
                exit(1); // If execvp returns, it failed
            } else {
                close(pipefd[0]);
                close(pipefd[1]);
            }
        }

        if (!background) {
            // Only wait for foreground processes
            waitpid(pid_first, &status, 0);
            if (pipe_index != -1) {
                waitpid(pid_second, &status, 0);
            }
        } else {
            handle_background_process(pid_first);
            if (pipe_index != -1) {
                handle_background_process(pid_second);
            }
        }
    }

    return 1; // Indicate successful execution
}

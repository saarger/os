#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

int locate_pipe_in_arglist(int count, char **arglist);

int execute_standard_command(char **arglist);

int execute_background_command(int count, char **arglist);

int execute_piped_command(int index, char **arglist);

int execute_output_redirection_command(int argc, char **argv);

// Define an enum for pipe read and write ends for clarity
typedef enum {
    READ_END = 0,
    WRITE_END = 1
} PipeEnds;

// Define an enum for function execution status
typedef enum {
    EXEC_FAIL = 0,
    EXEC_SUCCESS = 1
} ExecStatus;

// Define enum for standard file descriptors
typedef enum {
    STDIN = 0,
    STDOUT = 1,
    STDERR = 2
} StandardFileDescriptors;

int prepare(void) {
    // After prepare() is executed, the program will not terminate on receiving SIGINT.
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("Error - failed to modify SIGINT signal handling");
        return -1;
    }

    // Additionally, handle SIGCHLD signals to prevent zombie processes using ERAN'S TRICK.
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        perror("Error - failed to modify SIGCHLD signal handling");
        return -1;
    }

    return 0;
}

int process_arglist(int count, char **arglist) {
    // Evaluate each condition to determine the type of shell operation to execute
    int result = 0;
    int pipe_index;

    if (*arglist[count - 1] == '&') {
        result = execute_background_command(count, arglist);
    } else if (count > 1 && *arglist[count - 2] == '>') {
        result = execute_output_redirection_command(count, arglist);
    } else if ((pipe_index = locate_pipe_in_arglist(count, arglist)) != -1) {
        result = execute_piped_command(pipe_index, arglist);
    } else {
        result = execute_standard_command(arglist);
    }

    return result;
}

int finalize(void) {
    return 0;
}

int locate_pipe_in_arglist(int count, char **arglist) {
    // Search for the presence of the '|' symbol within the argument list; return its position if found
    for (int i = 0; i < count; i++) {
        if (*arglist[i] == '|') {
            return i;
        }
    }
    // Return -1 if the '|' symbol is not found
    return -1;
}

int execute_standard_command(char **arglist) {
    pid_t child_pid = fork();

    if (child_pid == -1) {
        perror("Error - failed to create a child process");
        return EXEC_FAIL;
    } else if (child_pid == 0) { // Child process
        // Reset signal handling for the child process to enable SIGINT termination
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            perror("Error - failed to reset SIGINT handling in child process");
            exit(1);
        }

        // Restore default SIGCHLD handling in case execvp doesn't change signals
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            perror("Error - failed to reset SIGCHLD handling in child process");
            exit(1);
        }

        // Execute the command and report an error if execution fails
        if (execvp(arglist[0], arglist) == -1) {
            perror("Error - command execution failed in child process");
            exit(1);
        }
    }

    // Parent process
    int status;
    if (waitpid(child_pid, &status, 0) == -1) {
        if (errno != ECHILD && errno != EINTR) {
            perror("Error - waitpid failed in parent process");
            return EXEC_FAIL;
        }
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int execute_background_command(int count, char **arglist) {
    // Create a new process for background execution
    pid_t child_pid = fork();

    if (child_pid == -1) { // Handle fork failure
        perror("Failed to create a background process");
        return EXEC_FAIL; // Indicate error to the parent process
    } else if (child_pid == 0) { // Child process
        // Remove the '&' argument from the argument list as it's not needed
        arglist[count - 1] = NULL;

        // Restore default SIGCHLD handling in case execvp doesn't change signals
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            perror("Failed to reset signal handling for background process");
            exit(EXIT_FAILURE);
        }

        // Execute the command and report an error if execution fails
        if (execvp(arglist[0], arglist) == -1) {
            perror("Failed to execute the command in the background process");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process
    return EXEC_SUCCESS; // Indicate success to the parent process
}



int execute_piped_command(int argc, char **argv) {
    int pipe_fds[2]; // File descriptors for the pipe
    argv[argc] = NULL; // Null-terminate the first part of the argument list

    if (pipe(pipe_fds) < 0) {
        perror("Failed to create pipe");
        return EXEC_FAIL;
    }

    pid_t pid1 = fork(); // Fork the first child process
    if (pid1 < 0) {
        perror("Forking first child failed");
        return EXEC_FAIL;
    } else if (pid1 == 0) { // First child process
        // Handle default signals
        if (signal(SIGINT, SIG_DFL) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            perror("Failed to set default signal handlers");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[READ_END]); // Close read end, not needed
        if (dup2(pipe_fds[WRITE_END], STDOUT_FILENO) < 0) { // Redirect stdout to pipe write end
            perror("Failed to redirect stdout to pipe");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[WRITE_END]);
        if (execvp(argv[0], argv) < 0) {
            perror("Execution of first command failed");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid2 = fork(); // Fork the second child process
    if (pid2 < 0) {
        perror("Forking second child failed");
        return EXEC_FAIL;
    } else if (pid2 == 0) { // Second child process
        // Handle default signals
        if (signal(SIGINT, SIG_DFL) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            perror("Failed to set default signal handlers");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[WRITE_END]); // Close write end, not needed
        if (dup2(pipe_fds[READ_END], STDIN_FILENO) < 0) { // Redirect stdin from pipe read end
            perror("Failed to redirect stdin from pipe");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[READ_END]);
        if (execvp(argv[argc + 1], &argv[argc + 1]) < 0) {
            perror("Execution of second command failed");
            exit(EXIT_FAILURE);
        }
    }

    // Close both ends of the pipe in the parent process
    close(pipe_fds[READ_END]);
    close(pipe_fds[WRITE_END]);

    // Wait for both child processes to complete
    if (waitpid(pid1, NULL, 0) < 0 && errno != ECHILD && errno != EINTR) {
        perror("Waiting for the first child process failed");
        return EXEC_FAIL;
    }
    if (waitpid(pid2, NULL, 0) < 0 && errno != ECHILD && errno != EINTR) {
        perror("Waiting for the second child process failed");
        return EXEC_FAIL;
    }

    return EXEC_SUCCESS; // Execution successful
}



int execute_output_redirection_command(int argc, char **argv) {
    // Null-terminate the command's argument list and prepare for redirection
    argv[argc - 2] = NULL;
    pid_t child_pid = fork(); // Create a child process

    if (child_pid < 0) { // Check if fork failed
        perror("Error - Forking failed");
        return EXEC_FAIL;
    } else if (child_pid == 0) { // In child process
        // Set signal handling to default for SIGINT and SIGCHLD
        if (signal(SIGINT, SIG_DFL) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            perror("Error - Setting default signal handlers failed");
            exit(EXIT_FAILURE);
        }
        // Open the output file with write-only access, create if not exists, truncate if exists
        int file_descriptor = open(argv[argc - 1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (file_descriptor < 0) {
            perror("Error - Opening file failed");
            exit(EXIT_FAILURE);
        }
        // Redirect standard output to the file
        if (dup2(file_descriptor, STDOUT) < 0) {
            perror("Error - Redirecting stdout to file failed");
            exit(EXIT_FAILURE);
        }
        close(file_descriptor); // Close the file descriptor as it's no longer needed

        // Execute the command
        if (execvp(argv[0], argv) < 0) {
            perror("Error - Executing command failed");
            exit(EXIT_FAILURE);
        }
    }

    // In parent process, wait for the child to complete
    if (waitpid(child_pid, NULL, 0) < 0 && errno != ECHILD && errno != EINTR) {
        perror("Error - Waiting for child process failed");
        return EXEC_FAIL;
    }

    return EXEC_SUCCESS; // Indicate successful execution
}
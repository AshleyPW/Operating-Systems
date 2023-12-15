#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define SIZE 128

char error_message[30] = "An error has occurred\n";

int is_closed, batch, old_path, new_path = 0;
char *path;
char paths[SIZE][SIZE];
int num_paths;

int process(char *command);
int new_process(char **wish_args);
int space(const char *buffer);
void print_error_message(void);

// Function to process a command from the user or a batch file.
int process(char *command) {
    // Check if the command is empty, just a newline, or a lone "&"
    if (command == NULL || command[0] == '\0' || command[0] == '\n' || strcmp(command, "&") == 0) {
        // Do nothing or handle as needed, but without printing an error
        return 0;
    }

    int stdout_cpy = 0; //keep track of the standard output file descriptor
    int return_code;
    char *tokens[SIZE];

    // Check if output redirection is requested.strchr returns pointer to first occurence of >
    if (strchr(command, '>') != NULL) {
        //tokenize the command and store in parts array
        char *parts[SIZE];
        parts[0] = strtok(strdup(command), " \n\t>");
        int part_count = 0; //error handling

        while (parts[part_count] != NULL) {
            part_count++;
            parts[part_count] = strtok(NULL, " \n\t>");
        }

        //if part_count is 1 with redirection, it is malformed input (no output file)
        if (part_count == 1) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return 0;
        }

        int token_count = 0; //keep track of parts of command 
        //tokenize the command on right using newline and tab and store in wish_args
        char *wish_args[SIZE];
        wish_args[0] = strtok(command, "\n\t>"); //command

        while (wish_args[token_count] != NULL) {  //args
            token_count++;
            wish_args[token_count] = strtok(NULL, " \n\t");
        }

        //if args associated with command on left are more than two, don't process
        if (token_count > 2) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return 0;
        }

        //tokenize output filename on right 
        int token_index = 0;
        tokens[0] = strtok(wish_args[1], " \n\t");

        while (tokens[token_index] != NULL) {
            token_index++;
            tokens[token_index] = strtok(NULL, " \n\t");
        }

        // Duplicate the standard output and redirect it to a file.
        char *fout = strdup(tokens[0]); // filename
        stdout_cpy = dup(STDOUT_FILENO); // duplicate stdout
        if (stdout_cpy == -1) {
            perror("dup failed");
            exit(EXIT_FAILURE);
        }

        int out = open(fout, O_WRONLY | O_CREAT | O_TRUNC, 0644); // open output file
        if (out == -1) {
            perror("Failed to open output file");
            exit(EXIT_FAILURE);
        }

        // Redirect stdout and stderr to the file
        if (dup2(out, STDOUT_FILENO) == -1 || dup2(out, STDERR_FILENO) == -1) {
            perror("dup2 failed");
            close(out);
            exit(EXIT_FAILURE);
        }

        close(out); // Close the file descriptor after duplication
        is_closed = 1;

        //if out is -1 this means there was an error during file open or redirection
        if (out == -1 || token_index > 1 || token_count > 2) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return 0;
        }
        //NULL terminate both arrays
        wish_args[token_count + 1] = NULL;
        tokens[token_index + 1] = NULL;
        strcpy(command, wish_args[0]); //the command to be executed after redirection
    }

    // After manipulating for redirection or to process a normal command
    if (command[0] != '\0' && command[0] != '\n') { //ensure a valid command to process
        //tokenize command and store in command_args array
        char *command_args[SIZE];
        command_args[0] = strtok(command, " \t\n");
        int arg_count = 0;

        while (command_args[arg_count] != NULL) {
            arg_count++;
            command_args[arg_count] = strtok(NULL, " \n\t");
        }
        command_args[arg_count + 1] = NULL; //NULL terminate array

        // if cd command
        if (strcmp(command_args[0], "cd") == 0) {
            if (arg_count == 2) { //if cd is followed by a possible directory, chdir()
                if (chdir(command_args[1]) != 0) { //if cd operations fails
                    write(STDERR_FILENO, error_message, strlen(error_message));
                }
            } else { //if no directory listed or more than one
                write(STDERR_FILENO, error_message, strlen(error_message));
            }
        } else if (strcmp(command_args[0], "path") == 0) { //if path command
            // set search paths
            new_path = 1;
            if (arg_count == 2) { //if there is two arguments set old_path to 0 and update path
                old_path = 0;
                path = strdup(command_args[1]);
                if (path[strlen(path) - 1] != '/') {
                    strcat(path, "/"); //ensure path ends with / to allow for setting a new search path
                }
            } else if (arg_count == 1) { //if there is one argument set search old_path to 1
                old_path = 1;
            } else { //more than 2 in arg_count, set old_path to 0 and update path
                old_path = 0;
                for (int i = 1; i < arg_count; i++) {
                    char *temp = strdup(command_args[i]);
                    if (temp[strlen(temp) - 1] != '/') {
                        strcat(temp, "/");
                    }
                    strcpy(paths[i - 1], temp);
                    num_paths++;
                }
            }
        } else if (strcmp(command_args[0], "exit") == 0) { //if exit command
            // Handle the 'exit' command.
            if (arg_count == 1) { //if one argument, exit successfully
                exit(0);
            } else { //if more than one argument output error - exit should not have mult args
                write(STDERR_FILENO, error_message, strlen(error_message));
            }
        } else {
            if (old_path == 1) { //if old_path is not 0, path was not set - error
                write(STDERR_FILENO, error_message, strlen(error_message));
            } else { //if path is set, execute command with new_process()
                return_code = new_process(command_args);
            }
        }
    }

    // Restore the standard output if it was redirected.
    if (is_closed == 1) {
        dup2(stdout_cpy, 1);
        is_closed = 0;
    }
    //return code returned by executed command  by new_process()
    return return_code;
}

// Function to execute a command.
int new_process(char **wish_args) {
    if (wish_args == NULL || wish_args[0] == NULL || wish_args[0][0] == '\0' || strcmp(wish_args[0], "&") == 0) {
        // Command is empty, null, or a lone "&"
        return 0; // Return without error
    }

    int f_return = fork(); //fork a new process
    //determine if parent or child process based on return val
    if (f_return < 0) { //val less than 0 is forking error
        print_error_message();
        exit(1);
    } else if (f_return == 0 && old_path != 1) { //if fork retuns 0, it is a child. check old_path to ensure there isn't a problem with search path
        // Child process
        if (new_path == 0) { // If new_path is 0, it sets path to "/bin/" and appends the command from wish_args
            path = strdup("/bin/");
            path = strcat(path, wish_args[0]);
            if (access(path, X_OK) != 0) { //access checks if path is executable 
                path = strdup("/usr/bin/");
                path = strcat(path, wish_args[0]);
                if (access(path, X_OK) != 0) { //if access returns a non-zero value it fails
                    print_error_message();
                    exit(0);
                }
            }
        } else if (new_path == 1 && num_paths == 0) { //If new_path is 1 and there are no specified paths, it uses the path as it is.
            path = strcat(path, wish_args[0]);
            if (access(path, X_OK) != 0) {
                print_error_message();
                exit(0);
            }
        } else { //If there are multiple paths, it iterates through them until it finds an executable file.
            for (int i = 0; i < num_paths; i++) {
                strcat(paths[i], wish_args[0]);
                if (access(paths[i], X_OK) == 0) {
                    strcpy(path, paths[i]);
                    break;
                }
            }
        } //If the access check is successful, it attempts to execute the command using execv.
        if (execv(path, wish_args) == -1) { //if -1, error executing command 
            print_error_message();
            exit(0);
        }
    } else {
        //parent process
        int status;
        if(waitpid(f_return, &status, 0) == -1){ //wait for the child process to finish
            print_error_message();
            exit(0);
        }
    }
    //function returns either process id of child or 0 for parent process
    return f_return;
}

// Function to check if a string contains only whitespace characters.
int space(const char *buffer) {
    for (int i = 0; i < strlen(buffer); i++) {
        if (!isspace(buffer[i])) {
            return 1;
        }
    }
    return 0;
}

// Function to print the error message and exit.
void print_error_message() {
    write(STDERR_FILENO, error_message, strlen(error_message));
    exit(1);
}

// Main function.
int main(int argc, char *argv[]) {
    FILE *file = NULL;
    path = (char *)malloc(SIZE);
    char buffer[SIZE];

    if (argc == 1) { // ./wish
        // Interactive mode: read commands from the user.
        while (1) {
            printf("wish> ");
            fflush(stdout);
            if (fgets(buffer, SIZE, stdin) == NULL) {
                print_error_message();
                continue;
            }
            if (space(buffer) == 0) { //checking if user input is empty
                continue; //if whitespace, skip current iteration
            }
            if (strstr(buffer, "&") != NULL) { //checking for parallelism. strstr returns pointer to first occurence of & symbol
                //tokenizing input to put commands inside wish_args array
                int i = 0;
                char *wish_args[SIZE];
                wish_args[0] = strtok(buffer, "\n\t&");
                while (wish_args[i] != NULL) {
                   if (space(wish_args[i]) == 0) { // Skip if the command is empty or just whitespace
                        i++;
                        continue;
                    }
                    i++;
                    wish_args[i] = strtok(NULL, "\n\t&");
                }
                wish_args[i] = NULL; // NULL terminate wish_args array
                //for every command in wish_args, start a process - run processes in parallel
                int pid[i]; 
                for (int j = 0; j < i; j++) {
                    pid[j] = process(wish_args[j]); 
                }
                //wait for each of the processes created above to finish
                for (int k = 0; k < i; k++) {
                    int return_status = 0; //a flag for the exit status of each process
                    waitpid(pid[k], &return_status, 0); //wait
                    if (return_status == 1) { //error message if exit status is 1
                        print_error_message();
                    }
                }
            } else { //run single command sequentially
                process(buffer);
            }
        }
    } else if (argc == 2) { // ./wish <filename>
    // Batch mode: read commands from a batch file.
    file = fopen(argv[1], "r"); //open specified file
    if (file == NULL) { //error check for file
        print_error_message();
        exit(1);
    }

    // Loop to read commands from file and store into buffer array
    while (fgets(buffer, SIZE, file)) {
        if (space(buffer) == 0) { //error check for empty lines or skip spaces
            continue;
        }

        if (strstr(buffer, "&") != NULL) { // Check for parallelism
            // Tokenizing input to put commands inside wish_args array
            int i = 0;
            char *wish_args[SIZE];
            wish_args[0] = strtok(buffer, "\n\t&");
            while (wish_args[i] != NULL) {
                if (space(wish_args[i]) == 0) { // Skip if the command is empty or just whitespace
                    i++;
                    continue;
                }
                i++;
                wish_args[i] = strtok(NULL, "\n\t&");
            }
            wish_args[i] = NULL; // NULL terminate wish_args array

            // Start each command as a separate process
            int pid[i];
            for (int j = 0; j < i; j++) {
                pid[j] = fork();
                if (pid[j] == 0) {
                    // Child process: Execute the command
                    process(wish_args[j]);
                    exit(0); // Exit after execution
                }
                // Error handling for fork() can be added here if needed
            }

            // Parent process: Wait for all child processes
            for (int k = 0; k < i; k++) {
                if (pid[k] > 0) {
                    int status;
                    waitpid(pid[k], &status, 0);
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
                        // Handle error if a child process exited with an error
                        print_error_message();
                    }
                }
            }
        } else {
            process(buffer); // For single commands
        }
    }

    fclose(file); // Close the file
} else {
    // error if not interactive or batch mode
    print_error_message();
}

    return 0;
}






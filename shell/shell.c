#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define INPUT_SIZE 500
#define MAX_ARGS 100
#define WRITE_END 1
#define READ_END 0
#define EXEC_FAIL 127

int main()
{
    // GETTING FIRST INPUT
    char str[INPUT_SIZE]; // input string
    printf("jsh$ ");
    fgets(str, INPUT_SIZE, stdin); // gets input
    if (ferror(stdin)) { // assert no error reading in
        fprintf(stderr, "Input Error\n");
        exit(1);
    }
    char* rest = str; // copy of input string
    rest[strcspn(rest, "\n")] = '\0'; // removes newline character

    // LOOP CHECKING FOR EXIT
    while(strcmp(rest, "exit") != 0) { // while rest != exit
        // SEPARATES EACH COMMAND
        char* commands[MAX_ARGS]; // holds list of commands
        int c = 0;
        char* temp;
        while ((temp = strtok_r(rest, "|", &rest))) { // seperates rest by |
            commands[c] = temp; // stores each | as a command
            c++;
        }
        commands[c] = NULL; // sets one after as null
        int last = c; // saves null index

        // GETS EACH ARG FROM EACH COMMAND
        char* args[MAX_ARGS][MAX_ARGS];
        for (int i = 0; i < last; i++) { // loops thru all commands
            c = 0;
            rest = commands[i]; // rest is a command
            while ((temp = strtok_r(rest, " ", &rest))) { // seperates by space
                args[i][c] = temp; // saves each arg in corresponding row
                c++;
            }
            args[i][c] = NULL; // sets last in each row to null
        }

        // MAKE PIPES
        int pipes[MAX_ARGS][2];
        for (int i = 0; i < last; i++) {
            if(pipe(pipes[i]) == -1) {
                fprintf(stderr, "pipe failure\n");
                exit(1);
            }
        }


        // EXECUTES EACH COMMAND USING ARGS
        int ids[MAX_ARGS];
        for (int i = last - 1; i >= 0; i--) { // loop thru each row of args
            int rc = fork(); // fork for each command
            if (rc < 0) { // fork failed
                fprintf(stderr, "fork failed\n");
                exit(1);
            }
            else if (rc == 0) { // child process
                // DUP PIPES
                if (last != 1) { // if multiple processes
                    int dr, dr2; // dup return used for err check
                    if (i == 0) { // first one only writes
                        dr = dup2(pipes[i][WRITE_END], STDOUT_FILENO);
                        if (dr < 0) {
                            fprintf(stderr, "dup failed\n");
                            exit(1);
                        }
                    }
                    else if (i == last - 1) { // last only reads
                        dr = dup2(pipes[i - 1][READ_END], STDIN_FILENO);
                        if (dr < 0) {
                            fprintf(stderr, "dup failed\n");
                            exit(1);
                        }
                    }
                    else { // everywhere else reads and writes
                        dr = dup2(pipes[i][WRITE_END], STDOUT_FILENO);
                        dr2 = dup2(pipes[i - 1][READ_END], STDIN_FILENO);
                        if (dr < 0 || dr2 < 0) {
                            fprintf(stderr, "dup failed\n");
                            exit(1);
                        }
                    }
                }

                // CLOSE PIPES
                for (int i = 0; i < last; i++) {
                    if(close(pipes[i][0]) == -1) {
                        if (errno != EBADF) { // avoid double close error
                            fprintf(stderr, "close failure\n");
                            exit(1);
                        }
                    }
                    if(close(pipes[i][1]) == -1) {
                        if (errno != EBADF) { // avoid double close error
                            fprintf(stderr, "close failure\n");
                            exit(1);
                        }
                    }
                }

                // EXEC
                execvp(args[i][0], args[i]); // exec row of args
                printf("jsh error: Command not found: %s\n", args[i][0]);
                exit(EXEC_FAIL); // exit with correct status
            }
            else { // parent
                // CLOSE PIPES
                if(close(pipes[i][0]) == -1) {
                    fprintf(stderr, "close failure\n");
                    exit(1);
                }
                if(close(pipes[i][1]) == -1) {
                    fprintf(stderr, "close failure\n");
                    exit(1);
                }
                ids[i] = rc; // sets ids at row to pid
            }
        }

        // WAITS FOR EACH CHILD PROCESS
        int status;
        for (int i = 0; i < last; i++) { // go thru each pid
            if(waitpid(ids[i], &status, 0) == -1) { // wait on pid
                fprintf(stderr, "wait failure\n");
                exit(1);
            }
        }
        printf("jsh status: %d\n", WEXITSTATUS(status)); // status of last pid

        // GETS NEXT INPUT
        printf("jsh$ ");
        fgets(str, INPUT_SIZE, stdin); // gets input
        if (ferror(stdin)) { // assert no error reading in
            fprintf(stderr, "Input Error\n");
            exit(1);
        }
        rest = str; // copy of input string
        rest[strcspn(rest, "\n")] = '\0'; // removes newline character
    }
    return 0;
}

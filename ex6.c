#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_CHILDREN 5

int main() {
    int child_pids[NUM_CHILDREN];
    int child_returns[NUM_CHILDREN];

    for (int i=0; i<NUM_CHILDREN; i++) {
        int pid = fork();
        if (pid == 0) {
            srand(time(NULL) ^ (getpid() << 16)); // send rand number
            int my_sleep = (rand() % 5) + 1;
            printf("[%d] Dormindo %d segundos...\n", getpid(), my_sleep);
            sleep(my_sleep);
            _exit(i+1);
        }
        child_pids[i] = pid; // only parent process executes
    }

    for (int i=0; i<NUM_CHILDREN; i++) {
        int status = 0;
        wait(&status);
        if (WIFEXITED(status)) {
            int result = WEXITSTATUS(status);
            child_returns[result] = result;
            printf("[original] Descendente de indice %d finalizou\n", result);
        }
    }

    printf("----------------------\n");

    for (int i=0; i<NUM_CHILDREN; i++) {
        printf("[original] Descendente de pid %d retornou %d\n", child_pids[i], child_returns[i]);
    }

    return 0;
}
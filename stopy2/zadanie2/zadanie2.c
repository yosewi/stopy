#define _GNU_SOURCE
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_N 100

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

int p;
volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t got_usr1 = 0;
volatile sig_atomic_t sender_pid = 0;
volatile sig_atomic_t parents_came = 0;

void usage(const char* pname){
    fprintf(stderr, "USAGE: %s t k n p\n", pname);
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int), int sigNo){
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if(-1 == sigaction(sigNo, &act, NULL)){
        ERR("sigaction");
    }
}

void sethandler_siginfo(void (*f)(int, siginfo_t *, void *), int sigNo){
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;

    if(sigaction(sigNo, &act, NULL) < 0)
        ERR("sigaction");
}


void last_handler(int sig){
    last_signal = sig;
}

void usr1_handler(int sig, siginfo_t *info, void *ucontext){
    got_usr1 = 1;
    sender_pid = info->si_pid;
}

void parents_handler(int sig){
    parents_came = 1;
}

void child_work(int is_ill_at_start, int k){
    int sick = is_ill_at_start;
    if(sick){
        alarm(k);
    }
    int cough_count = 0;
    sethandler(last_handler, SIGTERM);
    sethandler(parents_handler, SIGALRM);
    sethandler_siginfo(usr1_handler, SIGUSR1);
    srand(getpid());
    printf("Child[%d] starts day in the kindergarten, ill: %d\n", getpid(), sick);
    while(last_signal != SIGTERM && !parents_came){
        if(got_usr1){
            got_usr1 = 0;

            if(!sick){
                printf("Child[%d]: %d has coughed at me!\n", getpid(), sender_pid);

            int r = rand() % 100;
            if(r < p){
                sick = 1;
            printf("Child[%d] get sick\n", getpid());
            alarm(k);
        }
    }
}

        if(sick){
            int r = 50 + rand() % (200 - 50 + 1);
            struct timespec t = {0, 1000000L * r};
            nanosleep(&t, NULL);
            if(last_signal == SIGTERM){
                break;
            }
            kill(0,SIGUSR1);
            cough_count++;
            printf("Child[%d] is coughing %d\n", getpid(), cough_count);
        }
    }
    printf("Child[%d] exits\n", getpid());
    if(parents_came){
        printf("Coughed %d times and parents picked them up!\n", cough_count);
    }
    else{
        if(sick)
        printf("Coughed %d times and is still in the kindergarten!\n", cough_count);
    }
    exit(cough_count);
}

void create_children(int n, int k){
    pid_t s;
    for(int i=0;i<n;i++){
        if((s = fork()) < 0){
            ERR("fork");
        }
        if(s == 0){
            if(i == 0){
                child_work(1, k);
            }
            else{
                child_work(0, k);
            }
            exit(EXIT_SUCCESS);
        }
    }
}

void parent_work(int t){
    sethandler(last_handler, SIGALRM);
    sethandler(SIG_IGN, SIGUSR1);
    alarm(t);
    printf("KG[%d]: Alarm has been set for %d sec\n", getpid(), t);
    while(last_signal != SIGALRM) {}
    printf("Simulation has ended\n");
    kill(0, SIGTERM);
}

int main(int argc, char** argv){
    if(argc != 5)
        usage(argv[0]);

    int t = atoi(argv[1]);
    int k = atoi(argv[2]);
    int n = atoi(argv[3]);
    p     = atoi(argv[4]);

    if(t <= 0 || k <= 0 || n <= 0 || p <= 0)
        usage(argv[0]);

    sethandler(SIG_IGN, SIGTERM);
    sethandler(SIG_IGN, SIGUSR1);
    create_children(n, k);
    parent_work(t);
    while(wait(NULL) > 0){

    }

    return EXIT_SUCCESS;
}

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ftw.h>

#define MAX_LEN 1024
#define MAX_ARGS 64

#define ERR(source) \
    (kill(0, SIGKILL), perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(int argc, char* argv[])
{
    printf("%s p h\n", argv[0]);
    printf("\tp - path to directory describing the structure of the Austro-Hungarian office in Prague.\n");
    printf("\th - Name of the highest administrator.\n");
    exit(EXIT_FAILURE);
}
char src[MAX_LEN];
char dst[MAX_LEN];

char *args[MAX_ARGS];
int arg_count = 0;

int 

void clear_args(){
    for(int i = 0; i < arg_count; i++){
        free(args[i]);
        args[i] = NULL;
    }
    arg_count = 0;
}

void parse_input(char *line){
    clear_args();
    int len = strlen(line);
    if(len > 0 && line[len - 1] == '\n'){
        line[len - 1] = '\0';
    }

    char *ptr = line;
    int in_quote = 0;
    char buffer[MAX_LEN];
    int buf_id = 0;

    while(*ptr != '\0' && arg_count < MAX_ARGS){
        if(*ptr == '"'){
            in_quote = !in_quote;
            ptr++;
        }
        else if(*ptr == ' ' && !in_quote){
            if(buf_id > 0){
                buffer[buf_id] = '\0';
                args[arg_count++] = strdup(buffer);
                buf_id = 0;
            }
            ptr++;
        }
        else{
            if(buf_id < MAX_LEN - 1){
                buffer[buf_id++] = *ptr;
            }
            ptr++;
        }
    }
    if(buf_id > 0){
        buffer[buf_id] = '\0';
        args[arg_count++] = strdup(buffer);
    }
}

void cmd_add(){
    if(arg_count < 3){
        printf("Blad: add <source> <target1> [target2 ...]\n");
        return;
    }
    printf("Dodawanie kopii z %s do:\n", args[1]);
    for(int i = 2; i<arg_count;i++){
        printf("Cel %d: %s\n", i-1, args[i]);
    }
}

void cmd_end(){
    printf("Zakonczenie tworzenia kopii\n");
}

void cmd_list(){
    printf("lista kopii\n");
}

void cmd_restore(){
    printf("przywracanie kopii\n");
}

int main(int argc, char** argv){
    char line[MAX_LEN];

    printf(" System kopii zapasowych\n");
    printf("Dostepne komendy: add, end, list, restore, exit\n");

    while(1){
        if(fgets(line, sizeof(line), stdin) == NULL) break;

        parse_input(line);

        if(arg_count == 0){
            continue;
        }

        if(strcmp(args[0], "exit") == 0){
            printf("koniec\n");
            clear_args();
            break;
        }
        else if(strcmp(args[0], "add") == 0){
            cmd_add();
        }
        else if(strcmp(args[0], "end") == 0){
            cmd_end();
        }
        else if(strcmp(args[0], "list") == 0){
            cmd_list();
        }
        else if(strcmp(args[0], "restore") == 0){
            cmd_restore();
        }
        else{
            printf("jakis usage\n");
        }

        clear_args();
    }
    return 0;
}
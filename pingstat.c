#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#define BUF_SIZE 256

#define bool int
#define false 0
#define true 1

char* err_string;
char* prog_name;

void sig_ignore(int signum){
    // BRUH
}

void usage(char* mistake){
    if(*mistake){
        fprintf(stderr, "\e[1;36m%s\e[0m\n", mistake);
    }
    fprintf(stderr,
"USAGE: %s [OPTION]... [FILE]\n\
Write timestamp and latency (or loss) to FILE at given frequency if specified.\n\
Use command 'c' during runtime to visualize using gnuplot.\n\n\
With no FILE, data is written to 'data.bin'. If FILE is -, data is written to stdout.\n\n\
Options:\n\
  -p  maximum ping used for warnings and graphing\n\
  -f  freqency of ICMP echo requests in seconds\n\
  -i  IP to ping default is 1.1.1.1\n\
  -g  command to use for graphing. default is:\n\
        gnuplot -p -e \"set xdata time; set yrange [0<*:*<200]; plot 'data.bin'\n\
        binary format='%%long%%int%%*int' using (\\$1):2 with dots title 'latency (ms)',\n\
        'data.bin' binary format='%%long%%*int%%int' using (\\$1):2 lt rgb 'red'\n\
        title 'packet lost'\"\n\
  -a  append to FILE\n\
  -l  graph with lines instead of dots. Lines are more visible for short time frames.\n\
", prog_name);
    exit(1);
}

enum err_type{
    ERROR, WARN, INFO
};

void print_error(enum err_type type){
    time_t seconds;
    struct tm *timeStruct;
    seconds = time(NULL);
    timeStruct = localtime(&seconds);

    fprintf(stderr, "%02d:%02d:%02d ", timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
    if(!err_string){
        fprintf(stderr, "\e[1;31mERR:\e[0m UNKNOWN ERROR.\n");
    }else{
        switch(type){
            case(ERROR): fprintf(stderr, "\e[1;31mERROR: \e[0m"); break;
            case(WARN): fprintf(stderr, "\e[1;35mWARN: \e[0m"); break;
            case(INFO): fprintf(stderr, "\e[1;36mINFO: \e[0m");
        }
        fprintf(stderr, "%s", err_string);
    }
}

enum prog_name{PING, GNUPLOT};
static char *names[] = {"ping", "gnuplot", NULL};
static char *prog_full_path[] = {NULL, NULL};
void dependencies(){
    int count = sizeof(names)/sizeof(char *) - 1;
    const char *path = getenv("PATH");
    char *buf = malloc(strlen(path)+7);
    for(int i=0;*path;++path, ++i){
        if(*path == ':'){
            buf[i] = '/';
            int idx = i+1;
            for(char **nidx = names;*nidx;++nidx){
                strcpy(buf+idx, *nidx);
                if(access(buf, X_OK) == 0){
                    prog_full_path[nidx-names] = malloc(strlen(buf)+1);
                    strcpy(prog_full_path[nidx-names], buf);
                }
            }
            i = -1;
        } else{
            buf[i] = *path;
        }
    }
    for(int i=0; i<count; ++i){
        if(!prog_full_path[i]){
            (void)!asprintf(&err_string, "command '%s' was not found on the path.\n", names[i]);
            print_error(ERROR);
        }
    }
    free(buf);
}

struct arg_type{
    long maxPing;
    bool append;
    char *frequency, *ip, *graph_cmd, *outfile, *lines;
};

int main(int argc, char** argv){
    prog_name = argv[0];
    dependencies();
    if(!prog_full_path[PING]){
        (void)!asprintf(&err_string, "ping must be installed, as ICMP echo requests require root permissions.\n");
        print_error(ERROR);
        exit(5);
    }

    struct arg_type args;
    // DEFAULTS
    args.maxPing = 200;
    args.append = false;
    args.frequency = "1.0";
    args.ip = "1.1.1.1";
    args.outfile = "data.bin";
    args.graph_cmd = NULL;
    args.lines = "dots";

    // PARSE ARGS
    for(int i=1; i<argc; ++i){
        if(*(argv[i]) == '-'){
            if(i+1==argc && argv[i][1]!='a'&&argv[i][1]!='l'&&argv[i][1]!='h'&&argv[i][1]!='\0') usage("'-' has no corresponding argument.");
            switch(argv[i][1]){
                case 'p':
                    args.maxPing = strtol(argv[++i], NULL, 10);
                    if(args.maxPing<1) usage("max ping set too low");
                    break;
                case 'f': args.frequency = argv[++i]; break;
                case 'i': args.ip = argv[++i]; break;
                case 'g': args.graph_cmd = argv[++i]; break;
                case 'a': args.append = O_APPEND; break;
                case 'l': args.lines= "lines"; break;
                case '\0': args.outfile = "-"; break;
                default: usage("");
            }
        }else{
            args.outfile = argv[i];
        }
    }
    if(!args.graph_cmd && prog_full_path[GNUPLOT]) (void)!asprintf(&args.graph_cmd, 
        "%s -p -e \"set xdata time; set yrange [0<*:*<%ld]; plot '%s' binary format='%%long%%int%%*int'\
        using (\\$1):2 with %s title 'latency (ms)', '%s' binary format='%%long%%*int%%int' using (\\$1):2 lt rgb 'red'\
        title 'packet lost'\"", prog_full_path[GNUPLOT], args.maxPing, args.outfile,args.lines, args.outfile);
    
    if(strcmp(args.outfile, "-"))
        fprintf(stdout, "Press 'ENTER' to pause showing latency\nenter command 'g' to draw a graph.\nenter command 'l' to show latency.\n");

    char *ping_arguments[] = {prog_full_path[PING],/**"-c", "20",*/ "-i", args.frequency, args.ip, NULL};
    int status;
    int fd_pipe[2];
    if(pipe(fd_pipe)==-1){
        perror("pipe"); _exit(2);
    }
    int pid = fork();
    if(pid==0){ // PING
        if(dup2(fd_pipe[1], 1) == -1){
            perror("dup2");
            _exit(3);
        }
        if(dup2(fd_pipe[1], 2) == -1){
            perror("dup2");
            _exit(4);
        }
        close(fd_pipe[0]);
        close(fd_pipe[1]);
        execv("/bin/ping", ping_arguments);
        fprintf(stderr, "Failed to execute ping\n");
        _exit(1);
    }else{ // RECEIVER
        signal(SIGINT,sig_ignore);
        close(fd_pipe[1]);

        // SET STDIN NOT BLOCKING
        int fdflags = fcntl(0, F_GETFL, 0);
        fcntl(0, F_SETFL, fdflags | O_NONBLOCK);

        // OPEN outfile
        FILE *fpout;
        int fdout = 0;
        if(!strcmp(args.outfile, "-")){
            fpout = stdout;
            stdout = stderr;
        }
        else{
            if(!args.append) remove(args.outfile);
            fdout = open(args.outfile, O_WRONLY | args.append | O_CREAT, 0644);
            if(fdout == -1){
                perror(args.outfile);
                exit(3);
            }
            fpout = fdopen(fdout, "w");
        }

        char buf[BUF_SIZE], bufin[BUF_SIZE];
        int result, seq=0, last=1, llost=0;
        bool showLatency = true;
        float latency;
        int idx, iidx;
        long int data[2];
        printf("\e[?25l");
        while((idx = read(fd_pipe[0], buf, BUF_SIZE)) > 0){
            if((iidx = read(0, bufin, BUF_SIZE)) != -1){
                bufin[iidx-1] = 0;
                if(iidx==1){
                    showLatency = false;
                }else switch(*bufin){
                    case 'g':
                        if(args.outfile && !strcmp(args.outfile, "-")){
                            (void)!asprintf(&err_string, "The graph command expects data to be written to a file. This will fail if '-g' was not used correctly\n");
                            print_error(WARN);
                        }
                        fflush(fpout);
                        errno = 0;
                        (void)!asprintf(&err_string, "Attempting to draw graph.\n");
                        print_error(INFO);
                        int status = system(args.graph_cmd);
                        if(status != 0){
                            (void)!asprintf(&err_string, "Graphing failed.\n");
                            print_error(ERROR);
                            if(errno) perror("gnuplot");
                        }
                        break;
                    case 'l': 
                        showLatency = true;
                        break;
                    default:
                        (void)!asprintf(&err_string, "Command could not be parsed.\n\e[1;31m%s\n\e[0m^----------------------------------------------------\n", bufin);
                        print_error(ERROR);
                }
                
            }

            buf[idx-1] = 0;
            switch(*buf){
                case 'P': continue;
                case '/': continue;
                case ':':
                    // write packet lost
                    data[0] = time(NULL);
                    data[1] = (long)latency;
                    data[1] |= (long)1 << 32;
                    fwrite(data, sizeof(long int), 2, fpout);
                    llost = seq;
                    (void)!asprintf(&err_string, "%s\n", buf+2);
                    print_error(WARN);
                    break;
                case '-': 
                    (void)!asprintf(&err_string, "ping exited.\033[0K\n\e[1;36m%s\e[0m\n", buf);
                    print_error(INFO);
                    break;
                case '0'...'9':
                    if(buf[3] == 'b'){
                        errno = 0;
                        result = sscanf(buf, "%*d bytes from %*s icmp_seq=%d ttl=%*d time=%f ms", &seq, &latency);
                        if(result == 2){
                            if(latency > args.maxPing){
                                (void)!asprintf(&err_string, "latency exceeded %ld (%.0f)\n", args.maxPing, latency);
                                print_error(WARN);
                            }
                            if(last+1 != seq){
                                (void)!asprintf(&err_string, "PACKET LOSS (%d)\n", seq-last-1);
                                data[0] = time(NULL);
                                data[1] = (long)latency;
                                data[1] |= (long)10 << 32;
                                fwrite(data, sizeof(long int), 2, fpout);
                                print_error(WARN);
                                llost = seq;
                            }
                            last = seq;
                            // write latency
                            data[0] = time(NULL);
                            data[1] = (long)latency;
                            data[1] |= (long)-100 << 32;
                            fwrite(data, sizeof(long int), 2, fpout);
                        }else if(errno != 0){
                            perror("scanf");
                        }else{
                            for(int i=2;buf[idx-i]=='\n';--i) buf[idx-i] = 0;
                            (void)!asprintf(&err_string, "ping output could not be parsed.\n\e[1;31m%s\e[0m\n^----------------------------------------------------\n", buf);
                            print_error(ERROR);
                        }
                        break;
                    }
                    for(int i=2;buf[idx-i]=='\n';--i) buf[idx-i] = 0;
                default: 
                    if(buf[0] == 0) continue;
                    (void)!asprintf(&err_string, "ping output could not be parsed.\n\e[1;31m%s\e[0m\n^----------------------------------------------------\n", buf);
                    print_error(ERROR);
            }
            if(showLatency){
                if(llost == seq){
                    printf("Latency: \e[1;31mLOST\e[0m\033[0K\r"); fflush(stdout); continue;
                }
                printf("Latency: %.0fms\033[0K\r", latency); fflush(stdout);
            }
            
        }
        printf("\e[?25h");
        close(fd_pipe[0]);
        if(fdout) close(fdout);
        fclose(fpout);
        wait(&status);
        if(status){
            (void)!asprintf(&err_string, "ping exited with errors.\n");
            print_error(ERROR);
        }
    }
    return(0);
}
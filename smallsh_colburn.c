#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>

void check_fg(int exit_status);
char *get_pid_str();
void print_status(int exit_status);

#define MAX_ARGS 512
#define MAX_PROCESSES 512


int num_processes = 0;
int processes[MAX_PROCESSES] = {0};

void add_process(int process){
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] == 0)
        {
            processes[i] = process;
        }
    }
}

void remove_process(int process){
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] == process)
        {
            processes[i] = 0;
        }
    }
}

void kill_active_processes(){
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] != 0)
        {
            kill(processes[i], SIGKILL);
            processes[i] = 0;
        }
    }
}

void kill_bg_processes(){
    int status;
    int pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        remove_process(pid);
    }
    kill_active_processes();
}

struct command
{
    char *command;
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int is_bg;
};

struct sigaction sa;

// last_pid will be used to display the exit status of the last foreground process
int last_pid;
int last_exit_status;

// store currently running processes in foreground and background lists
int pid_list[10];
int pid_list_bg[10];
int allow_bg = 1;
/*
*  Signal handler for SIGSTSP
*  Exploration: Signal Handling API
*/
void handle_sigtstp(int sig)
{
    if (allow_bg == 1)
		{
			char* message = "Entering foreground-only mode (& is now ignored)\n";
			write(1, message, 49);    
			allow_bg = 0;
		}
		else
		{
			char* message = "Exiting foreground-only mode\n";
			write(1, message, 29);        
			allow_bg = 1;
		}
}

void redirect_bg(struct command *cmd)
{
    if (!cmd->input_file)
    {
        int source_fd = open("/dev/null", O_RDONLY);
        if (source_fd == -1)
        {
            perror("could not open /dev/null for input");
            exit(1);
        }
        int result = dup2(source_fd, 0);
        if (result == -1)
        {
            perror("dup2() failed for input redirection");
            exit(1);
        }
        fcntl(source_fd, F_SETFD, FD_CLOEXEC);
    }

    if (!cmd->output_file)
    {
        int target_fd = open("/dev/null", O_WRONLY, 0640);
        if (target_fd == -1)
        {
            perror("could not open /dev/null for output");
            exit(1);
        }
        int result = dup2(target_fd, 1);
        if (result == -1)
        {
            perror("dup2() failed for output redirection");
            exit(1);
        }
        fcntl(target_fd, F_SETFD, FD_CLOEXEC);
    }
}

void redirect(struct command *cmd)
{
    if (cmd->input_file)
    {
        int source_fd = open(cmd->input_file, O_RDONLY);
        if (source_fd == -1)
        {
            perror("could not open input file");
            exit(1);
        }
        int result = dup2(source_fd, 0);
        if (result == -1)
        {
            perror("dup2() failed for input redirection");
            exit(1);
        }
        fcntl(source_fd, F_SETFD, FD_CLOEXEC);
    }

    if (cmd->output_file)
    {
        int target_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (target_fd == -1)
        {
            perror("could not open output file");
            exit(1);
        }
        int result = dup2(target_fd, 1);
        if (result == -1)
        {
            perror("dup2() failed for output redirection");
            exit(1);
        }
        fcntl(target_fd, F_SETFD, FD_CLOEXEC);
    }
}

/*
*  Execute non-built-in commands with or without arguments
*  Exploration: Process API - Executing a New Program
*  Exploration: Process API - Monitoring Child Processes
*  Exploration: Process API - Creating and Terminating Processes
*/
int execute_command(struct command *cmd, struct sigaction sa)
{
    // initialize spawnpid to negative value to ensure variable do not contain a leftover value from a previous fork
    pid_t spawnpid = -1;

    // spawnpid will be 0 in the child
    // spawnpid will be child's pid in the parent
    spawnpid = fork();

    switch (spawnpid)
    {
        case -1:
            // -1 indicates that fork() failed and that the child does not exist
            perror("fork() failed\n");
            exit(1);
            break;
        
        case 0:
            // if spawnpid is 0, then we are in the child process, which will execute this code
            if (!cmd->is_bg || !allow_bg)
            {
                sa.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa, NULL);
            }

            if (!cmd->is_bg || !allow_bg)
            {
                redirect(cmd);
            }
            else
            {
                redirect_bg(cmd);
            }

            if (execvp(cmd->command, cmd->args))
            {
                char msg[256];
                sprintf(msg, "command failed: %s\n", cmd->command);
                perror(msg);
                exit(1);
            }
            break;
        default:
        if (!cmd->is_bg || !allow_bg)
            {
                waitpid(spawnpid, &last_exit_status, 0);
                check_fg(last_exit_status);
            }
        else
        {
            printf("background pid: %d\n", spawnpid);
            // add spawnpid to background list
            add_process(spawnpid);
        }
    }
    return 0;
}

void check_background(){
    int status;
    int pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        printf("child with PID %d terminated\n", pid);
        print_status(status);
    }
}

void check_fg(int exit_status)
{
    if(!WIFEXITED(exit_status)) 
    {
        printf("terminated by signal: %d\n", WTERMSIG(exit_status));
    } 
}

char *expand_token(char *s)
{
    const char* term = "$$";
	const char *replace = get_pid_str();
	
    if (!strstr(s, term)) {
        return strdup(s);
    }
  
    // line will be <= incoming line length, as len(^) < len(++)
    char *buf = malloc(2048);
    char* p = NULL;
    char* rest = s;    
    while ((p = strstr(rest, term))) {
        strncat(buf, rest, (size_t)(p - rest));
        strcat(buf, replace);
        rest = p + strlen(term);
    }    
    strcat(buf, rest);
    return buf;   
}

char *get_pid_str() {
    int pid = getpid();
    char *s = malloc(32);
    sprintf(s, "%d", pid);
    return s;
}

int is_valid(char *s) {
    if (!s) return 0;
    if (s[0] == '\0') return 0;
    if (s[0] == '\n') return 0;
    if (s[0] == '#') return 0;
    return 1;
}

int find_symbol(char *tokens[MAX_ARGS], char *symbol) {
    for (int i = 0; i < MAX_ARGS; i++)
    {
        if (!tokens[i])
        {
            break;
        }
        if (!strcmp(tokens[i], symbol))
        {
            return i;
        }
    }
    return -1;
}

char *safe(char *s) {
    if (s) return s;
    return "NULL";
}

int is_symbol(char *s) {
    if (!strcmp(s, ">")) return 1;
    if (!strcmp(s, "<")) return 1;
    if (!strcmp(s, "&")) return 1;
    return 0;
}

void print_command(struct command *cmd) {
    printf("command: %s\n", safe(cmd->command));
    printf("input: %s\n", safe(cmd->input_file));
    printf("output: %s\n", safe(cmd->output_file));
    printf("background: %d\n", cmd->is_bg);
    for (int i = 0; i < MAX_ARGS; i++)
    {
        if (!cmd->args[i])
        {
            break;
        }

        printf("%d | %s\n", i, safe(cmd->args[i]));
    }
}

struct command *parse_line(char *line)
{
    // initialize variables and allocate initial memory
    char *tokens[MAX_ARGS] = {0};
    char *saveptr;
    int token_idx = 0;
    char *token = strtok_r(line, " \n", &saveptr);
    if (token) 
    {
        tokens[0] = expand_token(token);
        token_idx++;
        while ((token = strtok_r(NULL, " \n", &saveptr)) != NULL)
        {
            tokens[token_idx] = expand_token(token);
            token_idx++;
        }
    }
    int gt = find_symbol(tokens, ">");
    int lt = find_symbol(tokens, "<");
    int amp = find_symbol(tokens, "&");

    struct command* cmd = malloc(sizeof(struct command));
    memset(cmd, 0, sizeof(struct command));
    if (gt != -1) 
    {
        cmd->output_file = strdup(tokens[gt+1]);
    }
    if (lt != -1) 
    {
        cmd->input_file = strdup(tokens[lt+1]);
    }
    if (amp != -1) 
    {
        cmd->is_bg = 1;
    }

    for (int i = 0; i < MAX_ARGS; i++)
    {
        if (!tokens[i])
        {
            break;
        }
        if (i == gt || i == lt || i == amp)
        {
            break;
        }
        cmd->args[i] = strdup(tokens[i]);
    }
    cmd->command = strdup(cmd->args[0]);
    return cmd;
}

struct command *read_line(void)
{
    char *input_line = NULL;
    size_t len = 0;

    // get input from user and save in var input_line
    GET_INPUT:
    check_background();
    printf(": ");                       // prompt user to input command and arguments
    fflush(stdout);
    getline(&input_line, &len, stdin);
    if (ferror(stdin))
    {
        clearerr(stdin);
        goto GET_INPUT;
    }
    if (!is_valid(input_line))
    {
        input_line = NULL;
        len = 0;
        goto GET_INPUT;
    }

    // call function to parse line into command and args
    struct command *cmd = parse_line(input_line);
    
    // free memory
    free(input_line);
    return cmd;
}

void print_status(int exit_status)
{
    if(WIFEXITED(exit_status)) 
    {
        printf("exit value: %d\n", WEXITSTATUS(exit_status));
    }
    else 
    {
        printf("terminated by signal: %d\n", WTERMSIG(exit_status));
    }    
}

void run_built_in(struct command *cmd) {
    if (strcmp(cmd->command, "cd") == 0)
    {
        if (cmd->args[1] == NULL)
        {
            // if no args given, change to home dir
            // https://www.stev.org/post/cgethomedirlocationinlinux
            char *home_dir = getenv("HOME");
            chdir(home_dir);
        }
        else
        {
            chdir(cmd->args[1]);
        }
    }
    else if (strcmp(cmd->command, "status") == 0)
    {
        print_status(last_exit_status);
    }
    else if (strcmp(cmd->command, "exit") == 0)
    {
        kill_bg_processes();
        exit(0);
    }
}

int is_built_in(char *s) {
    if (!strcmp(s, "cd") || !strcmp(s, "status") || !strcmp(s, "exit"))
    {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    // Run command loop
    // prepare to handle signals
    // Exploration: Signal Handling API
    // https://www.youtube.com/watch?v=jF-1eFhyz1U
    struct sigaction sigint = {{0}};
    sigint.sa_handler = SIG_IGN;
    sigint.sa_flags = 0;
    sigaction(SIGINT, &sigint, NULL);

    struct sigaction sigtstp = {{0}};
    sigtstp.sa_handler = &handle_sigtstp;
    sigtstp.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp, NULL);

    // fill pid_list with junk value
    memset(pid_list, (-5), 10 * sizeof(*pid_list));
    do {
        struct command *cmd = read_line();
        // print_command(cmd);
        // printf("-------------------------------------\n");
        if (is_built_in(cmd->command))
        {
            run_built_in(cmd);
        } 
        else
        {
            execute_command(cmd, sigint);
        }
        
    } while (1);                       // program should continue until user calls exit

    return 0;
}
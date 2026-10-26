#include <stdio.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

static int run_cmd(char *cmd)
{
    int rc;
    pid_t pid;
    char *argv[4];
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = 0;

    if (posix_spawn(&pid, "/bin/sh", 0, 0, argv, environ) == -1) {
        fprintf(stderr, "FAILED TO EXEC '%s'\n", cmd);
        return 1;
    }
    if (waitpid(pid, &rc, 0) == -1) {
        fprintf(stderr, "FAILED TO WAIT FOR PROCESS FIN\n");
        return 1;
    }
    if (WIFEXITED(rc) && !WEXITSTATUS(rc)) {
        fprintf(stderr, "procd OK\n");
        return 0;
    }
    fprintf(stderr, "'%s' failed\n", cmd);
    return 1;
}

int main(void)
{
    char *cmd0 = "ls -la";
    char *cmd1 = "(cd .xtrs && ls -la)";
    char *cmd2 = "ls -la";
    if (run_cmd(cmd0)) return 1;
    if (run_cmd(cmd1)) return 1;
    if (run_cmd(cmd2)) return 1;
    return 0;
}

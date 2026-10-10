
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#define MAX_INPUT 1024
#define MAX_JOBS 64
#define MAX_ARGS 64

typedef enum {
    JOB_RUNNING,
    JOB_DONE
} JobState;

typedef struct {
    int id;
    pid_t pid;
    pid_t pgid;
    char command[MAX_INPUT];
    JobState state;
} Job;

static Job jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_id = 1;

static void updateJobs(void)
{
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state != JOB_RUNNING)
            continue;

        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);

        if (result == jobs[i].pid) {
            jobs[i].state = JOB_DONE;
            printf("\n[%d] Done  %s\n",
                   jobs[i].id, jobs[i].command);
        } else if (result < 0 && errno == ECHILD) {
            jobs[i].state = JOB_DONE;
        } else if (result < 0 && errno != EINTR) {
            perror("waitpid");
        }
    }
}

static void removeCompletedJobs(void)
{
    int write_index = 0;

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state == JOB_RUNNING) {
            if (write_index != i)
                jobs[write_index] = jobs[i];

            write_index++;
        }
    }

    job_count = write_index;
}

static void listJobs(void)
{
    updateJobs();

    if (job_count == 0) {
        printf("No active jobs.\n");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        printf("[%d] %-8s PID=%ld PGID=%ld  %s\n",
               jobs[i].id,
               jobs[i].state == JOB_RUNNING ? "Running" : "Done",
               (long)jobs[i].pid,
               (long)jobs[i].pgid,
               jobs[i].command);
    }

    removeCompletedJobs();
}

static int addJob(pid_t pid, const char *command)
{
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "Job table is full.\n");
        return -1;
    }

    Job *job = &jobs[job_count];

    job->id = next_job_id++;
    job->pid = pid;
    job->pgid = pid;
    job->state = JOB_RUNNING;

    snprintf(job->command, sizeof(job->command), "%s", command);

    job_count++;

    printf("[%d] %ld\n", job->id, (long)pid);
    return 0;
}

static int tokenize(char *input, char *args[])
{
    int argc = 0;
    char *saveptr = NULL;
    char *token = strtok_r(input, " \t", &saveptr);

    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    args[argc] = NULL;
    return argc;
}

static void launchBackground(char *args[], const char *command)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Create a separate process group for this job. */
        if (setpgid(0, 0) < 0) {
            perror("setpgid");
            _exit(1);
        }

        execvp(args[0], args);
        perror(args[0]);
        _exit(127);
    }

    /* Also set the process group from the parent side. */
    if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
        perror("setpgid");

    if (addJob(pid, command) < 0) {
        kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
    }
}

static void executeForeground(char *args[])
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        execvp(args[0], args);
        perror(args[0]);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        perror("waitpid");
        break;
    }
}

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 16: Background Jobs and Job Table\n");
    printf("Examples:\n");
    printf("  sleep 5 &\n");
    printf("  jobs\n");
    printf("  echo hello\n");
    printf("  exit\n");

    while (1) {
        updateJobs();

        printf("skill16> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0')
            continue;

        if (strcmp(input, "exit") == 0)
            break;

        if (strcmp(input, "jobs") == 0) {
            listJobs();
            continue;
        }

        char original[MAX_INPUT];
        snprintf(original, sizeof(original), "%s", input);

        size_t len = strlen(input);
        int background = 0;

        while (len > 0 && (input[len - 1] == ' ' ||
                           input[len - 1] == '\t'))
            input[--len] = '\0';

        if (len > 0 && input[len - 1] == '&') {
            background = 1;
            input[--len] = '\0';
        }

        char *args[MAX_ARGS];
        int argc = tokenize(input, args);

        if (argc == 0)
            continue;

        if (background)
            launchBackground(args, original);
        else
            executeForeground(args);

        updateJobs();
    }

    /* Reap any jobs still running before exiting. */
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state == JOB_RUNNING) {
            kill(jobs[i].pid, SIGTERM);

            while (waitpid(jobs[i].pid, NULL, 0) < 0 &&
                   errno == EINTR) {
            }
        }
    }

    printf("Exiting Skill 16.\n");
    return 0;
}

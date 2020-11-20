#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/msg.h>
#include "queue.h"
#include "shared.h"
#include "string.h"

int ipcid;
Shared *data;
int toChildQueue;
int toMasterQueue;
char *filen;
int childCount = 19;

FILE *o;

#define MAX_LINES 20000
const int CLOCK_ADD_INC = 5000000;
int NUM_SHARED = -1;
int VERBOSE_LEVEL = 0;
long lineCount = 0;

int deadlockCount = 0;
int deadlockProcs = 0;

int pidreleases = 0;
int pidallocs = 0;
int pidprocterms = 0;

void Handler(int signal);
void DoFork(int value);
void ShmAttatch();
void TimerHandler(int sig);
int SetupInterrupt();
int SetupTimer();
void DoSharedWork();
int FindEmptyProcBlock();
void SweepProcBlocks();
void AddTimeLong(Time *time, long amount);
void AddTime(Time *time, int amount);
int FindPID(int pid);
void QueueAttatch();
void GenerateResources();
void DisplayResources();
int AllocResource(int procRow, int resID);
int FindAllocationRequest(int procRow);

struct
{
        long mtype;
        char mtext[100];
} msgbuf;

void AddTime(Time *time, int amount)
{
        int newnano = time->ns + amount;

        while (newnano >= 1000000000)
        {
                newnano -= 1000000000;
                (time->seconds)++;
        }

        time->ns = newnano;
}

void AddTimeLong(Time *time, long amount)
{
        long newnano = time->ns + amount;

        while (newnano >= 1000000000)
        {
                newnano -= 1000000000;
                (time->seconds)++;
        }

        time->ns = (int)newnano;
}

int CompareTime(Time *time1, Time *time2)
{
        long time1Epoch = ((long)(time1->seconds) * (long)1000000000) + (long)(time1->ns);
        long time2Epoch = ((long)(time2->seconds) * (long)1000000000) + (long)(time2->ns);

        if (time1Epoch > time2Epoch)
                return 1;

        else
                return 0;
}

void Handler(int signal)
{
        fflush(stdout);

        int i;

        if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                DisplayResources();

        if (VERBOSE_LEVEL == 1)
        {
                printf("\n\n\n*** STATUSES ***\n");

                for (i = 0; i < childCount; i++)
                {
                        printf("%i: %s\n", i, data->proc[i].status);
                }
        }

        double ratio = ((double)deadlockProcs) / ((double)pidprocterms);

        printf("\n\n*** Stats ***\n\n\ Resource Allocations: %i\n\ Resource Releases: %i\n\n\ Process Terminations: %d\n\ Process Deadlock Proc Kills: %d\n\ Process Deadlock Count: %i\n\ Process Deadlock to Normal Death Ratio: %f\n\n", pidallocs, pidreleases, pidprocterms, deadlockProcs, deadlockCount, ratio);

        for (i = 0; i < childCount; i++)
                if (data->proc[i].pid != -1)
                        kill(data->proc[i].pid, SIGTERM);

        fflush(o);
        fclose(o);

        shmctl(ipcid, IPC_RMID, NULL);

        msgctl(toChildQueue, IPC_RMID, NULL);
        msgctl(toMasterQueue, IPC_RMID, NULL);

        printf("%s: Termination signal caught. Killing all processes.\n\n", filen);

        kill(getpid(), SIGTERM);
}

void DoFork(int value)
{
        char *forkarg[] = {"./user", NULL};

        execv(forkarg[0], forkarg);
        Handler(1);
}

void ShmAttatch()
{
        key_t shmkey = ftok("shmshare", 312);

        if (shmkey == -1)
        {
                fflush(stdout);
                perror("Error: Ftok failed");
                return;
        }

        ipcid = shmget(shmkey, sizeof(Shared), 0600 | IPC_CREAT);

        if (ipcid == -1)
        {
                fflush(stdout);
                perror("Error: failed to get shared memory");
                return;
        }

        data = (Shared *)shmat(ipcid, (void *)0, 0);

        if (data == (void *) - 1)
        {
                fflush(stdout);
                perror("Error: failed to attach to shared memory");
                return;
        }
}

void TimerHandler(int sig)
{
        Handler(sig);
}

int SetupInterrupt()
{
        struct sigaction act;

        act.sa_handler = TimerHandler;
        act.sa_flags = 0;

        return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

int SetupTimer()
{
        struct itimerval value;

        value.it_interval.tv_sec = 2;
        value.it_interval.tv_usec = 0;

        value.it_value = value.it_interval;

        return (setitimer(ITIMER_PROF, &value, NULL));
}

int FindEmptyProcBlock()
{
        int i;

        for (i = 0; i < childCount; i++)
        {
                if (data->proc[i].pid == -1)
                        return i;
        }

        return -1;
}

void SweepProcBlocks()
{
        int i;

        for (i = 0; i < MAX_PROCS; i++)
                data->proc[i].pid = -1;
}

void GenerateResources()
{
        int i;

        for (i = 0; i < 20; i++)
        {
                data->resVec[i] = (rand() % 10) + 1;
                data->allocVec[i] = data->resVec[i];
        }

        NUM_SHARED = (rand() % 4) + 2;

        for (i = 0; i < NUM_SHARED; i++)
        {
                while (1)
                {
                        int tempval = rand() % 20;

                        if (CheckForExistence(data->sharedRes, NUM_SHARED, tempval) == -1)
                        {
                                data->sharedRes[i] = tempval;
                                break;
                        }
                }
        }

        if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                DisplayResources();
}

int CheckForExistence(int *values, int size, int value)
{
        int i;

        for (i = 0; i < size; i++)
                if (values[i] == value)
                        return 1;

        return -1;
}

void DisplayResources()
{
        fprintf(o, "\n\n#### Beginning print of resource tables ####\n\n");
        fprintf(o, "*** Allocated Resources ***\nX -> resources, Y -> process\n");
        fprintf(o, "Proc ");

        int i;

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", i);
        }

        int j;

        for (i = 0; i < childCount; i++)
        {
                fprintf(o, "\n %3i|", i);

                for (j = 0; j < 20; j++)
                        fprintf(o, "%4i", data->alloc[j][i]);
        }

        fprintf(o, "\n\n\n*** Requested Resources ***\nX -> resources, Y -> processes\n");
        fprintf(o, "Proc ");

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", i);
        }

        for (i = 0; i < childCount; i++)
        {
                fprintf(o, "\n %3i|", i);

                for (j = 0; j < 20; j++)
                        fprintf(o, "%4i", data->req[j][i]);
        }

        fprintf(o, "\n\n\n *** Resource Vector ***\n");

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", i);
        }

        fprintf(o, "\n");

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", data->resVec[i]);
        }

        fprintf(o, "\n\n\n *** Allocation Vector ***\n");

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", i);
        }

        fprintf(o, "\n");

        for (i = 0; i < 20; i++)
        {
                fprintf(o, "%3i ", data->allocVec[i]);
        }

        fprintf(o, "\n\n\n *** Shared Resource IDs *** \n");

        for (i = 0; i < NUM_SHARED; i++)
        {
                fprintf(o, "%3i ", data->sharedRes[i]);
        }

        fprintf(o, "\n\n#### Ending printing resource tables ####\n\n");
}

int FindAllocationRequest(int procRow)
{
        int i;

        for (i = 0; i < 20; i++)
                if (data->req[i][procRow] > 0)
                        return i;
}

int AllocResource(int procRow, int resID)
{
        while (data->allocVec[resID] > 0 && data->req[resID][procRow] > 0)
        {
                if (CheckForExistence(&(data->sharedRes), NUM_SHARED, resID) == -1)
                {
                        (data->allocVec[resID])--;
                }

                (data->alloc[resID][procRow])++;
                (data->req[resID][procRow])--;
        }

        if (data->req[resID][procRow] > 0)
                return -1;

        return 1;
}

void DeleteProc(int procrow, struct Queue *queue)
{
        int i;

        for (i = 0; i < 20; i++)
        {
                if (CheckForExistence(&(data->sharedRes), NUM_SHARED, i) == -1)
                        data->allocVec[i] += data->alloc[i][procrow];

                if (data->alloc[i][procrow] > 0)
                        pidreleases++;

                data->alloc[i][procrow] = 0;
                data->req[i][procrow] = 0;
        }

        int temp;

        for (i = 0; i < getSize(queue); i++)
        {
                temp = dequeue(queue);

                if (temp == data->proc[procrow].pid || temp == -1)
                        continue;

                else
                        enqueue(queue, temp);
        }
}

void DellocResource(int procRow, int resID)
{
        if (CheckForExistence(&(data->sharedRes), NUM_SHARED, resID) == -1)
        {
                (data->allocVec[resID]) += (data->alloc[resID][procRow]);
        }

        pidreleases++;
        data->alloc[resID][procRow] = 0;
}

int FindPID(int pid)
{
        int i;

        for (i = 0; i < childCount; i++)
                if (data->proc[i].pid == pid)
                        return i;

        return -1;
}

void DeadLockDetector(int *procFlags)
{
        int *tempVec = calloc(20, sizeof(int));
        int i, j;
        int isEnding = 0;
        int updated;

        for (i = 0; i < 20; i++)
                tempVec[i] = data->allocVec[i];

        do
        {
                updated = 0;

                for (i = 0; i < childCount; i++)
                {
                        if ((procFlags[i] == 1) || (data->proc[i].pid < 0))
                                continue;

                        isEnding = 1;

                        for (j = 0; j < 20; j++)
                        {
                                if ((data->req[j][i]) > 0 && (tempVec[j] - data->req[j][i]) < 0)
                                {
                                        isEnding = 0;
                                }
                        }

                        procFlags[i] = isEnding;

                        if (isEnding == 1)
                        {
                                updated = 1;

                                for (j = 0; j < 20; j++)
                                        tempVec[j] += data->alloc[j][i];
                        }
                }
        } while (updated == 1);

        free(tempVec);
}

void DoSharedWork()
{
        int activeProcs = 0;
        int exitCount = 0;
        int status;
        int iterator;
        int requestCounter = 0;
        int msgsize;

        data->sysTime.seconds = 0;
        data->sysTime.ns = 0;

        Time nextExec = {0, 0};
        Time deadlockExec = {0, 0};

        struct Queue *resQueue = createQueue(childCount);

        while (1)
        {
                AddTime(&(data->sysTime), CLOCK_ADD_INC);

                pid_t pid;

                if (activeProcs < childCount && CompareTime(&(data->sysTime), &nextExec))
                {
                        pid = fork();

                        if (pid < 0)
                        {
                                perror("Failed to fork, exiting");
                                Handler(1);
                        }

                        if (pid == 0)
                        {
                                DoFork(pid);
                        }

                        nextExec.seconds = data->sysTime.seconds;
                        nextExec.ns = data->sysTime.ns;

                        AddTimeLong(&nextExec, abs((long)(rand() % 501) * (long)1000000));

                        int pos = FindEmptyProcBlock();

                        if (pos > -1)
                        {
                                data->proc[pos].pid = pid;

                                if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                        fprintf(o, "%s: [%i:%i] [PROC CREATE] pid: %i\n\n", filen, data->sysTime.seconds, data->sysTime.ns, pid);

                                activeProcs++;
                        }

                        else
                        {
                                kill(pid, SIGTERM);
                        }
                }

                if ((msgsize = msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), 0, IPC_NOWAIT)) > -1)
                {
                        if (strcmp(msgbuf.mtext, "REQ") == 0)
                        {
                                int reqpid = msgbuf.mtype;
                                int procpos = FindPID(msgbuf.mtype);
                                int resID = 0;
                                int count = 0;

                                msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), reqpid, 0);
                                resID = atoi(msgbuf.mtext);

                                msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), reqpid, 0);
                                count = atoi(msgbuf.mtext);

                                data->req[resID][procpos] = count;

                                if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                        fprintf(o, "%s: [%i:%i] [REQUEST] pid: %i proc: %i resID: %i\n", filen, data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype, procpos, resID);

                                if (AllocResource(procpos, resID) == -1)
                                {
                                        enqueue(resQueue, reqpid);

                                        if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                                fprintf(o, "\t-> [%i:%i] [REQUEST] pid: %i request not done...\n\n", data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype);
                                }

                                else
                                {
                                        pidallocs++;

                                        strcpy(msgbuf.mtext, "REQUEST GRANTED");

                                        msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT);

                                        if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                                fprintf(o, "\t-> [%i:%i] [REQUEST] pid: %i request done...\n\n", data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype);
                                }
                        }

                        else if (strcmp(msgbuf.mtext, "REL") == 0)
                        {
                                int reqpid = msgbuf.mtype;
                                int procpos = FindPID(msgbuf.mtype);

                                msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), reqpid, 0);
                                DellocResource(procpos, atoi(msgbuf.mtext));

                                if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                        fprintf(o, "%s: [%i:%i] [RELEASE] pid: %i proc: %i resID: %i\n\n", filen, data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype, FindPID(msgbuf.mtype), atoi(msgbuf.mtext));
                        }

                        else if (strcmp(msgbuf.mtext, "TER") == 0)
                        {
                                int procpos = FindPID(msgbuf.mtype);

                                if (procpos > -1)
                                {
                                        DeleteProc(procpos, resQueue);

                                        if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                                fprintf(o, "%s: [%i:%i] [TERMINATE] pid: %i proc: %i\n\n", filen, data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype, FindPID(msgbuf.mtype));

                                        pidprocterms++;
                                }
                        }

                        if ((requestCounter++) == 19)
                        {
                                if (VERBOSE_LEVEL = 1 && lineCount++ < MAX_LINES)
                                        DisplayResources();

                                requestCounter = 0;
                        }
                }

                if ((pid = waitpid((pid_t) - 1, &status, WNOHANG)) > 0)
                {
                        if (WIFEXITED(status))
                        {
                                if (WEXITSTATUS(status) == 21)
                                {
                                        exitCount++;
                                        activeProcs--;

                                        int position = FindPID(pid);

                                        if (position > -1)
                                                data->proc[position].pid = -1;
                                }
                        }
                }

                if (CompareTime(&(data->sysTime), &deadlockExec))
                {
                        deadlockExec.seconds = data->sysTime.seconds;
                        deadlockExec.ns = data->sysTime.ns;

                        AddTimeLong(&deadlockExec, abs((long)(rand() % 1000) * (long)1000000));

                        int *procFlags;
                        int i;
                        int deadlockDisplayed = 0;
                        int terminated;

                        do
                        {
                                terminated = 0;
                                procFlags = calloc(childCount, sizeof(int));

                                DeadLockDetector(procFlags);

                                for (i = 0; i < childCount; i++)
                                {
                                        if (procFlags[i] == 0 && data->proc[i].pid > 0)
                                        {
                                                if (deadlockDisplayed == 0)
                                                {
                                                        deadlockCount++;
                                                        deadlockDisplayed = 1;

                                                        if (lineCount++ < MAX_LINES)
                                                        {
                                                                fprintf(o, "*** DEADLOCK DETECTED ***");

                                                                DisplayResources();

                                                                int j;

                                                                fprintf(o, "Deadlocked Procs are as follows:\n [ ");

                                                                for (j = 0; j < childCount; j++)
                                                                        if (procFlags[j] == 0)
                                                                                fprintf(o, "%i ", j);

                                                                fprintf(o, "]\n");
                                                        }
                                                }

                                                terminated = 1;

                                                msgbuf.mtype = data->proc[i].pid;
                                                strcpy(msgbuf.mtext, "DIE");

                                                msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT);

                                                DeleteProc(i, resQueue);

                                                pidprocterms++;
                                                deadlockProcs++;

                                                if (lineCount++ < MAX_LINES)
                                                        fprintf(o, "%s: [%i:%i] [KILL SENT] pid: %i proc: %i\n\n", filen, data->sysTime.seconds, data->sysTime.ns, data->proc[i].pid, i);
                                                break;
                                        }
                                }

                                free(procFlags);
                        } while (terminated == 1);
                }

                for (iterator = 0; iterator < getSize(resQueue); iterator++)
                {
                        int cpid = dequeue(resQueue);
                        int procpos = FindPID(cpid);
                        int resID = FindAllocationRequest(procpos);

                        if (procpos < 0)
                        {
                                continue;
                        }

                        else if (AllocResource(procpos, resID) == 1)
                        {
                                if (VERBOSE_LEVEL == 1 && lineCount++ < MAX_LINES)
                                        fprintf(o, "%s: [%i:%i] [REQUEST] [QUEUE] pid: %i request done...\n\n", filen, data->sysTime.seconds, data->sysTime.ns, msgbuf.mtype);

                                pidallocs++;

                                strcpy(msgbuf.mtext, "REQUEST GRANTED");
                                msgbuf.mtype = cpid;

                                msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT);
                        }

                        else
                        {
                                enqueue(resQueue, cpid);
                        }
                }

                fflush(stdout);
        }

        shmctl(ipcid, IPC_RMID, NULL);

        msgctl(toChildQueue, IPC_RMID, NULL);
        msgctl(toMasterQueue, IPC_RMID, NULL);

        fflush(o);
        fclose(o);
}

void QueueAttatch()
{
        key_t shmkey = ftok("shmsharemsg", 766);

        if (shmkey == -1)
        {
                fflush(stdout);
                perror("./oss: Error: Ftok failed");
                return;
        }

        toChildQueue = msgget(shmkey, 0600 | IPC_CREAT);

        if (toChildQueue == -1)
        {
                fflush(stdout);
                perror("./oss: Error: toChildQueue creation failed");
                return;
        }

        shmkey = ftok("shmsharemsg2", 767);

        if (shmkey == -1)
        {
                fflush(stdout);
                perror("./oss: Error: Ftok failed");
                return;
        }

        toMasterQueue = msgget(shmkey, 0600 | IPC_CREAT);

        if (toMasterQueue == -1)
        {
                fflush(stdout);
                perror("./oss: Error: toMasterQueue creation failed");
                return;
        }
}

int main(int argc, int **argv)
{
        filen = argv[0];

        srand(time(NULL) ^ (getpid() << 16));

        if (SetupInterrupt() == -1)
        {
                perror("./oss: Failed to setup Handler for SIGPROF");
                return 1;
        }

        if (SetupTimer() == -1)
        {
                perror("./oss: Failed to setup ITIMER_PROF interval timer");
                return 1;
        }

        int optionItem;

        while ((optionItem = getopt(argc, argv, "hvn:")) != -1)
        {
                switch (optionItem)
                {
                        case 'h':
                                printf("\t%s Help Menu\n\ \t-h : Show Help Dialog \n\ \t-v : Enable verbose mode. Default: off \n\ \t-n [count] : Max processes at the same time. Default: 19\n\n", filen);
                                return;

                        case 'v':
                                VERBOSE_LEVEL = 1;
                                printf("%s: Verbose mode enabled...\n", argv[0]);

                                break;

                        case 'n':
                                childCount = atoi(optarg);

                                if (childCount > 19 || childCount < 0)
                                {
                                        printf("%s: Max -n is 19. Must be greater than 0.\n", argv[0]);
                                        return -1;
                                }

                                printf("\n%s: Info: Set max concurrent children to: %s", argv[0], optarg);

                                break;

                        case '?':
                                printf("\n%s: Error: Invalid argument or arguments missing. Use -h to see usage.", argv[0]);
                                return;
                }
        }

        o = fopen("output.log", "w");

        if (o == NULL)
        {
                perror("oss: Failed to open output file: ");
                return 1;
        }

        ShmAttatch();
        QueueAttatch();
        SweepProcBlocks();
        GenerateResources();
        signal(SIGINT, Handler);
        DoSharedWork();

        return 0;
}
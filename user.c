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

int CHANCE_TO_DIE_PERCENT = 1;
const int CHANCE_TO_REQUEST = 55;

Shared *data;
int toChildQueue;
int toMasterQueue;
int ipcid;
char *filen;
int pid;

void ShmAttatch();
void QueueAttatch();
void AddTime(Time *time, int amount);
int FindPID(int pid);
int CompareTime(Time *time1, Time *time2);
void AddTimeLong(Time *time, long amount);

struct
{
        long mtype;
        char mtext[100];
} msgbuf;

int FindPID(int pid)
{
        int i;
        for (i = 0; i < MAX_PROCS; i++)
                if (data->proc[i].pid == pid)
                        return i;

        return -1;
}

void AddTime(Time *time, int amount)
{
        int newnano = time->ns + amount;

        while (newnano >= 1000000000)
        {
                newnano -= 1000000000;
                (time->seconds)++;
        }

        time->ns = (int)newnano;
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

void QueueAttatch()
{
        key_t shmkey = ftok("shmsharemsg", 766);

        if (shmkey == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: Ftok failed");
                return;
        }

        toChildQueue = msgget(shmkey, 0600 | IPC_CREAT);

        if (toChildQueue == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: toChildQueue creation failed");
                return;
        }

        shmkey = ftok("shmsharemsg2", 767);

        if (shmkey == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: Ftok failed");
                return;
        }

        toMasterQueue = msgget(shmkey, 0600 | IPC_CREAT);

        if (toMasterQueue == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: toMasterQueue creation failed");
                return;
        }
}

void ShmAttatch()
{
        key_t shmkey = ftok("shmshare", 312);

        if (shmkey == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: Ftok failed");
                return;
        }

        ipcid = shmget(shmkey, sizeof(Shared), 0600 | IPC_CREAT);

        if (ipcid == -1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: failed to get shared memory");
                return;
        }

        data = (Shared *)shmat(ipcid, (void *)0, 0);

        if (data == (void *)-1)
        {
                printf("\n%s: ", filen);
                fflush(stdout);
                perror("Error: failed to attach to shared memory");
                return;
        }
}

void CalcNextActionTime(Time *t)
{
        t->seconds = data->sysTime.seconds;
        t->ns = data->sysTime.ns;
        long mstoadd = (rand() % 251) * 1000000;
        AddTimeLong(t, mstoadd);
}

int getResourceToRelease(int pid)
{
        int myPos = FindPID(pid);
        int i;

        for (i = 0; i < 20; i++)
        {
                if (data->alloc[i][myPos] > 0)
                        return i;
        }

        return -1;
}

int main(int argc, int argv)
{
        ShmAttatch();
        QueueAttatch();

        pid = getpid();

        Time nextActionTime = {0,0};

        srand(pid);

        int resToReleasePos;

        while (1)
        {
                strcpy(data->proc[FindPID(pid)].status, "START NEW LOOP");

                if (CompareTime(&(data->sysTime), &(nextActionTime)) == 1)
                {
                        strcpy(data->proc[FindPID(pid)].status, "ENTER TIME START");

                        if ((rand() % 100) <= CHANCE_TO_DIE_PERCENT)
                        {
                                msgbuf.mtype = pid;

                                strcpy(msgbuf.mtext, "TER");
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER TERM");
                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0);

                                strcpy(data->proc[FindPID(pid)].status, "EXIT MASTER");

                                exit(21);
                        }

                        resToReleasePos = getResourceToRelease(pid);

                        if ((rand() % 100) < CHANCE_TO_REQUEST)
                        {
                                strcpy(data->proc[FindPID(pid)].status, "ENTER REQUEST BLOCK");
                                int resToRequest = (rand() % 20);

                                msgbuf.mtype = pid;

                                strcpy(msgbuf.mtext, "REQUEST");
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER REQUEST");

                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0);

                                char *convert[5];

                                sprintf(convert, "%i", resToRequest);

                                msgbuf.mtype = pid;

                                strcpy(msgbuf.mtext, convert);
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER REQUESTED POSITION");

                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0);

                                int resCount = abs((rand() % ((data->resVec[resToRequest] - (data->alloc[resToRequest][FindPID(pid)]) + 1 ))));

                                sprintf(convert, "%i", resCount);

                                msgbuf.mtype = pid;

                                strcpy(msgbuf.mtext, convert);
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER REQUEST NUMBER");

                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0);

                                strcpy(data->proc[FindPID(pid)].status, "WAIT MASTER GRANT");

                                do
                                {
                                        msgrcv(toChildQueue, &msgbuf, sizeof(msgbuf), pid, 0);

                                        if (strcmp(msgbuf.mtext, "REQUEST GRANT") == 0 || strcmp(msgbuf.mtext, "DIE") == 0)
                                                break;
                                } while (1);

                                if (strcmp(msgbuf.mtext, "DIE") == 0)
                                {
                                        CHANCE_TO_DIE_PERCENT = 1000;
                                        CalcNextActionTime(&nextActionTime);
                                        continue;
                                }

                                strcpy(data->proc[FindPID(pid)].status, "GOT REQUEST GRANT");

                                CalcNextActionTime(&nextActionTime);
                        }

                        else if (resToReleasePos >= 0)
                        {
                                strcpy(data->proc[FindPID(pid)].status, "START RELEASE");

                                msgbuf.mtype = pid;

                                strcpy(msgbuf.mtext, "REL");
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER RELEASE REQUEST");

                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT);

                                char *convert[5];

                                sprintf(convert, "%i", resToReleasePos);

                                strcpy(msgbuf.mtext, convert);
                                strcpy(data->proc[FindPID(pid)].status, "SEND MASTER RELEASE ID");

                                msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0);

                                strcpy(data->proc[FindPID(pid)].status, "MASTER ACCEPT RELEASE ID");

                                CalcNextActionTime(&nextActionTime);
                        }

                        else
                        {
                                CalcNextActionTime(&nextActionTime);
                        }
                }
        }
}
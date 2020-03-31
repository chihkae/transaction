#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "tmanager.h"
#include <sys/mman.h>
#include <strings.h>
#include "msg.h"
#include <poll.h>
#include <sys/time.h>

void usage(char *cmd)
{
  printf("usage: %s  portNum\n",
         cmd);
}

int main(int argc, char **argv)
{

  // This is some sample code feel free to delete it

  unsigned long port;
  char logFileName[128];
  int logfileFD;

  if (argc != 2)
  {
    usage(argv[0]);
    return -1;
  }
  char *end;
  int err = 0;

  port = strtoul(argv[1], &end, 10);
  if (argv[1] == end)
  {
    printf("Port conversion error\n");
    exit(-1);
  }

  // Create the TX manager socket
  int sockfd;
  struct sockaddr_in servAddr;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("socket creation failed");
    exit(-1);
  }
  int optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

  // Setup my server information
  memset(&servAddr, 0, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons(port);
  // Accept on any of the machine's IP addresses.
  servAddr.sin_addr.s_addr = INADDR_ANY;

  // Bind the socket to the requested addresses and port
  if (bind(sockfd, (const struct sockaddr *)&servAddr,
           sizeof(servAddr)) < 0)
  {
    perror("bind failed");
    exit(-1);
  }

  // At this point our socket is setup and ready to go so we can interact with
  // workers.

  /* got the port number create a logfile name */
  snprintf(logFileName, sizeof(logFileName), "TXMG_%u.log", port);

  logfileFD = open(logFileName, O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR);
  if (logfileFD < 0)
  {
    char msg[256];
    snprintf(msg, sizeof(msg), "Opening %s failed", logFileName);
    perror(msg);
    exit(-1);
  }

  // Let's see if the logfile has some entries in it by checking the size

  struct stat fstatus;
  if (fstat(logfileFD, &fstatus) < 0)
  {
    perror("Filestat failed");
    exit(-1);
  }

  if (fstatus.st_size < sizeof(struct transactionSet))
  {
    // Just write out a zeroed file struct
    printf("Initializing the log file size\n");
    struct transactionSet tx;
    bzero(&tx, sizeof(tx));
    if (write(logfileFD, &tx, sizeof(tx)) != sizeof(tx))
    {
      printf("Writing problem to log\n");
      exit(-1);
    }
  }

  struct transactionSet *txlog = mmap(NULL, 512, PROT_READ | PROT_WRITE, MAP_SHARED, logfileFD, 0);
  if (txlog == NULL)
  {
    perror("Log file could not be mapped in:");
    exit(-1);
  }
  struct pollfd pfds[1];
  pfds[0].fd = sockfd;
  pfds[0].events = POLLIN; //monitor to see if there is packet to read from socket
  int fd_count = 1;        //only one pollfd-sockfd

  if (!txlog->initialized)
  {
    printf("log file not initailized\n");
    int i;
    for (i = 0; i < MAX_TX; i++)
    {
      txlog->transaction[i].tstate = TX_NOTINUSE;
      txlog->transaction[i].workersParticipating = 0;
      txlog->transaction[i].inUse = 0;
    }

    txlog->initialized = 1;
    // Make sure in memory copy is flushed to disk
    msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE);
  }
  else
  {
    printf("recovery phase \n");
    
    //abort transactions in progress or voting stage once recovered
    struct tx *ptr = txlog->transaction;
    for (int i = 0; i < MAX_TX; i++)
    {
      if ((ptr[i].tstate == TX_INPROGRESS || ptr[i].tstate == TX_VOTING) && (ptr[i].inUse == 1))
      {
        printf("setting to recovery state\n");
        ptr[i].tstate = TX_Recovering;
      }
      printf("state:%d",txlog->transaction[0].tstate);
      if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
      {
        perror("Msync problem");
      }
    }
  }

  printf("Starting up Transaction Manager on %d\n", port);
  printf("Port number:              %d\n", port);
  printf("Log file name:            %s\n", logFileName);

  int n;
  int transactionIndex;
  int poll_count;
  while (1)
  {
    //process recovery state
    //if we crashed and recovered, tell workers that the transaction is aborted for txns that
    //have not been commited or aborted
    struct tx *ptr = txlog->transaction;

    for (int i = 0; i < MAX_TX; i++)
    {
      printf("after recovery state:%d",ptr[0].tstate);
      if (ptr[i].tstate == TX_Recovering)
      {
        struct sockaddr_in *addresses = ptr[i].worker;
        for (int j = 0; j < MAX_WORKERS; j++)
        {
          if (&(addresses[j]) != NULL)
          {
            printf("sending aborted mssg\n");
            twoPCMssg *recoverymssg = (twoPCMssg*)malloc(sizeof(twoPCMssg));
            recoverymssg->ID = ptr[i].txID;
            recoverymssg->msgKind = aborted;
            struct sockaddr* ptr2 = (struct sockaddr*)&addresses[j];
            int bytesSent = sendto(sockfd, (twoPCMssg *)recoverymssg,
                                   sizeof(twoPCMssg), 0, (struct sockaddr *)ptr2, sizeof(*ptr2));
            printf("bytesSent:%d",bytesSent);
          }
        }
        ptr[i].tstate = TX_ABORTED;
        ptr[i].inUse = 0;
      }
    }

    poll_count = poll(pfds, fd_count, 2000);
    if (poll_count == -1)
    {
      printf("poll error\n");
    }

    //timeout scenario
    if (poll_count == 0)
    {
      printf("timeout");
      //abort transaction if 10 seconds have elapsed since commitRequest
      struct tx *ptr = txlog->transaction;
      struct timeval end;
      gettimeofday(&end,0);
      for (int i = 0; i < MAX_TX; i++)
      {
        if (ptr[i].tstate == TX_VOTING && ptr[i].inUse == 1)
        {
          unsigned long elapsedTime = (end.tv_sec) - (ptr[i].start.tv_sec);
          printf("timeelpased %lu\n",elapsedTime);
          printf("preparedVotes:%d",ptr[i].preparedVotes);
          printf("workersparticipating:%d\n",ptr[i].workersParticipating);
          printf("true votercount:%d\n",(ptr[i].preparedVotes < ptr[i].workersParticipating));
          if(elapsedTime > 10.0){
            printf("true");
          }else{
            printf("false\n");
          }
          if ((ptr[i].preparedVotes < ptr[i].workersParticipating) && (elapsedTime > 10.0))
          {
            printf("timed out and aborted transaction\n");
            ptr[i].tstate = TX_ABORTED;
            ptr[i].inUse = 0;
            if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
            {
              perror("Msync problem");
            }
            struct sockaddr_in *addresses = ptr[i].worker;

            for (int j = 0; j < MAX_WORKERS; j++)
            {
              if (&(addresses[j]) != NULL)
              {
                twoPCMssg *abortMssg = malloc(sizeof(twoPCMssg));
                abortMssg->ID = ptr[i].txID;
                abortMssg->msgKind = aborted;
                struct sockaddr* ptr2 = (struct sockaddr*)&addresses[j];
                int bytesSent = sendto(sockfd, (twoPCMssg *)abortMssg,
                                       sizeof(twoPCMssg), 0, (struct sockaddr *)ptr2, sizeof(*ptr2));
              }
            }
          }
        }
      }
    }
    else if (pfds[0].revents & POLLIN)
    {
      printf("poll in\n");
      struct sockaddr_in client;
      socklen_t len;
      twoPCMssg *buff = (twoPCMssg *)malloc(sizeof(twoPCMssg));
      int n = recvfrom(sockfd, (struct twoPCMssg *)buff, sizeof(struct twoPCMssg *), MSG_WAITALL,
                       (struct sockaddr *)&client, &len);
      if (n < 0)
      {
        perror("Receiving error:");
        abort();
      }
      printf("Got a packet\n");

      if (buff->msgKind == beginTransaction)
      {
        int index = -1;
        printf("beginning transaction\n");
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].inUse == 0)
          {
            index = i;
            ptr[i].inUse = 1;
            ptr[i].tstate = TX_INPROGRESS;
            ptr[i].txID = buff->ID;
            ptr[i].worker[0] = client;
            ptr[i].preparedVotes = 0;
            ptr[i].workersParticipating = 1;
            break;
          }
        }
        printf("index:%d",index);
        printf("transaction state:%d\n",txlog->transaction[0].tstate);
        if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
        {
          perror("Msync problem");
        }
      }
      else if (buff->msgKind == joiningWorker)
      {
        printf("joining transaction\n");
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            ptr[i].workersParticipating = ptr[i].workersParticipating + 1;
            int indexInWorker = ptr[i].workersParticipating - 1;
            printf("workers participating:%d\n",ptr[i].workersParticipating);
            printf("index of joining worker: %d\n",indexInWorker);
            if (indexInWorker < 6)
            {
              ptr[i].worker[indexInWorker] = client;
            }
            break;
          }
        }
        if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
        {
          perror("Msync problem");
        }
      }
      else if (buff->msgKind == commitRequest || buff->msgKind == commitRequestCrash)
      {
        printf("commitRequest or commitRequestCrash\n");
        int index = -1;
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            index = i;
            break;
          }
        }
        if (index != -1)
        {
          printf("index:%d\n",index);
          printf("commit request index not -1\n");
          struct tx *transaction = &(txlog->transaction[index]);
          if(buff->msgKind == commitRequestCrash){
            *(&(transaction->tstate)) = TX_VOTING_CRASH;
          }else{
            *(&(transaction->tstate)) = TX_VOTING;
          }
          if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
          {
            perror("Msync problem");
          }
          printf("TX state:%d\n", txlog->transaction[index].tstate);
          twoPCMssg *response = (twoPCMssg*)malloc(sizeof(twoPCMssg));
          response->ID = buff->ID;
          response->msgKind = prepareToCommit;

          for (int i = 0; i < MAX_WORKERS; i++)
          {
            if (&(transaction->worker[i]) != NULL)
            {
              printf("trnsaction worker %d address not null\n",i);
              int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                     sizeof(twoPCMssg), 0, (struct sockaddr *)&(transaction->worker[i]), sizeof(struct sockaddr_in));
              printf("sent\n");
            }
          }
          struct timeval start;
          gettimeofday(&start,0);
          transaction->start = start;
          printf("transaction->start_t:%d",transaction->start);
          if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
          {
            perror("Msync problem");
          }
        }
      }
      else if (buff->msgKind == votingDecision)
      {
        printf("received voting decision mssg\n");
        int index = -1;
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            index = i;
            break;
          }
        }
        if (index != -1)
        {
          struct tx *transaction = &txlog->transaction[index];
          if (transaction->tstate == TX_COMMITTED)
          {
            twoPCMssg *response = malloc(sizeof(twoPCMssg));
            response->ID = buff->ID;
            response->msgKind = commited;
            for (int i = 0; i < MAX_WORKERS; i++)
            {
              if (&(transaction->worker[i]) != NULL)
              {
                printf("trnsaction worker %d address not null\n",i);
                int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                     sizeof(twoPCMssg), 0, (struct sockaddr *)&(transaction->worker[i]), sizeof(struct sockaddr_in));
                  printf("sent\n");
              }
            }
          }
          else if (transaction->tstate == TX_ABORTED || transaction->tstate == TX_Recovering)
          {
            twoPCMssg *response = malloc(sizeof(twoPCMssg));
            response->ID = buff->ID;
            response->msgKind = aborted;
            for (int i = 0; i < MAX_WORKERS; i++)
            {
              if (&(transaction->worker[i]) != NULL)
              {
                printf("trnsaction worker %d address not null\n",i);
                int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                     sizeof(twoPCMssg), 0, (struct sockaddr *)&(transaction->worker[i]), sizeof(struct sockaddr_in));
                printf("sent\n");
              }
            }
          }
        }
      }
      else if (buff->msgKind == aborttxn)
      {
        printf("received abort txn\n");
        int index = -1;
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            index = i;
            break;
          }
        }
        if (index != -1)
        {
          struct tx *transaction = &txlog->transaction[index];
          if (transaction->tstate == TX_INPROGRESS || transaction->tstate == TX_VOTING || transaction->tstate == TX_VOTING_CRASH)
          {
            transaction->tstate = TX_ABORTED;
            transaction->inUse = 0;
            if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
            {
              perror("Msync problem");
            }
            twoPCMssg *response = malloc(sizeof(twoPCMssg));
            response->ID = buff->ID;
            response->msgKind = aborted;
            for (int i = 0; i < MAX_WORKERS; i++)
            {
              if (&(transaction->worker[i]) != NULL)
              {
                int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                       sizeof(twoPCMssg), 0, (struct sockaddr *)&transaction->worker[i], sizeof(struct sockaddr_in));
              }
            }
          }
        }
      }
      else if (buff->msgKind == abortandcrashtxn)
      {
        printf("received abort and crash txn\n");
        int index = -1;
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            index = i;
            break;
          }
        }
        if (index != -1)
        {
          struct tx *transaction = &txlog->transaction[index];
          if (transaction->tstate == TX_INPROGRESS || transaction->tstate == TX_VOTING)
          {
            _exit(0);
          }
        }
      }
      else
      {
        printf("in else case\n");
        int index = -1;
        struct tx *ptr = txlog->transaction;
        for (int i = 0; i < MAX_TX; i++)
        {
          if (ptr[i].txID == buff->ID)
          {
            index = i;
            break;
          }
        }
        printf("index %d", index);
        if (index != -1)
        {
          printf("index not -1\n");
          struct tx *transaction = &(txlog->transaction[index]);
          printf("transaction tstate:%d",transaction->tstate);
          if (transaction->tstate == TX_VOTING || transaction->tstate == TX_VOTING_CRASH)
          {
            printf("tx voting\n");
            if (buff->msgKind == prepared)
            {
              printf("received prepared\n");
              transaction->preparedVotes += 1;
              if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
              {
                perror("Msync problem");
              }
            }
            else if (buff->msgKind == no)
            {
              if(transaction->tstate == TX_VOTING_CRASH){
                printf("aborted transaction but will crash before announcing\n");
                transaction->tstate = TX_ABORTED;
                transaction->inUse = 0;
                if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
                {
                  perror("Msync problem");
                }
                _exit(0);
              }
              transaction->tstate = TX_ABORTED;
              transaction->inUse = 0;
              if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
              {
                perror("Msync problem");
              }
              twoPCMssg *response = malloc(sizeof(twoPCMssg));
              response->ID = buff->ID;
              response->msgKind = aborted;
              for (int i = 0; i < MAX_WORKERS; i++)
              {
                if ((&transaction->worker[i]) != NULL)
                {
                  int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                         sizeof(twoPCMssg), 0, (struct sockaddr *)&transaction->worker[i], sizeof(struct sockaddr_in));
                }
              }
            }
            if (transaction->preparedVotes == transaction->workersParticipating)
            {
              printf("commited\n");
              if(transaction->tstate == TX_VOTING_CRASH){
                printf("commited but gonna crash before relaying back decision\n");
                transaction->tstate = TX_COMMITTED;
                transaction->inUse = 0;
                if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
                {
                  perror("Msync problem");
                }
                _exit(0);
              }
              transaction->tstate = TX_COMMITTED;
              transaction->inUse = 0;
              if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
              {
                perror("Msync problem");
              }
              twoPCMssg *response = malloc(sizeof(twoPCMssg));
              response->ID = buff->ID;
              response->msgKind = commited;
              for (int i = 0; i < MAX_WORKERS; i++)
              {
                if ((&transaction->worker[i]) != NULL)
                {
                  int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                         sizeof(twoPCMssg), 0, (struct sockaddr *)&transaction->worker[i], sizeof(struct sockaddr_in));
                }
              }
            }
          }
        }
      }
    }
  }
}

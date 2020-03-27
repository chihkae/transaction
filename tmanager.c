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
    int i;
    for (i = 0; i < MAX_TX; i++)
    {
      txlog->transaction[i].tstate = TX_NOTINUSE;
      txlog->numWorkersInTransaction[i] = 0;
    }

    txlog->initialized = 1;
    // Make sure in memory copy is flushed to disk
    msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE);
  }
  else
  {
    //abort transactions in progress or voting stage once recovered
    struct tx *ptr = txlog->transaction;
    for (int i = 0; i < MAX_TX; i++)
    {
      if (ptr[i].tstate == TX_INPROGRESS || TX_VOTING)
      {
        ptr[i].tstate == TX_Recovering;
      }
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

    struct tx *ptr = txlog->transaction;

    for (int i = 0; i < MAX_TX; i++)
    {
      if (ptr[i].tstate == TX_Recovering)
      {
        struct sockaddr_in *addresses = ptr[i].worker;
        for (int j = 0; j < MAX_WORKERS; j++)
        {
          if (&(addresses[j]) != NULL)
          {
            twoPCMssg *recoverymssg = malloc(sizeof(twoPCMssg));
            bzero(&recoverymssg, sizeof(twoPCMssg));
            recoverymssg->ID = ptr[i].txID;
            recoverymssg->msgKind = aborted;
            int bytesSent = sendto(sockfd, (twoPCMssg *)recoverymssg,
                                   sizeof(twoPCMssg), 0, (struct sockaddr *)&addresses[j], sizeof(struct sockaddr_in));
          }
        }
      }
    }

    poll_count = poll(pfds, fd_count, 10000);

    //timeout scenario
    if (poll_count == 0)
    {
      //abort transaction if 10 seconds have elapsed since commitRequest
      struct tx *ptr = txlog->transaction;
      clock_t end_t = clock();
      for (int i = 0; i < MAX_TX; i++)
      {
        if (ptr[i].tstate == TX_VOTING)
        {
          double timeElapsed = ((double)(end_t - ptr[i].start_t)) / CLOCKS_PER_SEC;
          if (ptr[i].preparedVotes < ptr[i].workersParticipating && timeElapsed > 10.0)
          {
            ptr[i].tstate = TX_ABORTED;
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
                bzero(&abortMssg, sizeof(twoPCMssg));
                abortMssg->ID = ptr[i].txID;
                abortMssg->msgKind = aborted;
                int bytesSent = sendto(sockfd, (twoPCMssg *)abortMssg,
                                       sizeof(twoPCMssg), 0, (struct sockaddr *)&addresses[j], sizeof(struct sockaddr_in));
              }
            }
          }
        }
      }
    }
    else if (pfds[0].revents & POLLIN)
    {
      //abort transaction if 10 seconds have elapsed since commitRequest
      struct tx *ptr = txlog->transaction;
      clock_t end_t = clock();
      for (int i = 0; i < MAX_TX; i++)
      {
        if (ptr[i].tstate == TX_VOTING)
        {
          double timeElapsed = ((double)(end_t - ptr[i].start_t)) / CLOCKS_PER_SEC;
          if (ptr[i].preparedVotes < ptr[i].workersParticipating && timeElapsed > 10.0)
          {
            ptr[i].tstate = TX_ABORTED;
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
                bzero(&abortMssg, sizeof(twoPCMssg));
                abortMssg->ID = ptr[i].txID;
                abortMssg->msgKind = aborted;
                int bytesSent = sendto(sockfd, (twoPCMssg *)abortMssg,
                                       sizeof(twoPCMssg), 0, (struct sockaddr *)&addresses[j], sizeof(struct sockaddr_in));
              }
            }
          }
        }
      }

      struct sockaddr_in client;
      socklen_t len;
      twoPCMssg *buff = malloc(sizeof(twoPCMssg));
      bzero(&buff, sizeof(twoPCMssg));
      bzero(&client, sizeof(client));
      int n = recvfrom(sockfd, (struct twoPCMssg *)buff, sizeof(struct twoPCMssg *), MSG_WAITALL,
                       (struct sockaddr *)&client, &len);
      if (n < 0)
      {
        perror("Receiving error:");
        abort();
      }
      printf("Got a packet\n");

      if (buff->msgKind == beginTransaction || buff->msgKind == joiningWorker)
      {
        int transactionIndex = buff->ID % MAX_TX;
        int workerIndex = txlog->numWorkersInTransaction[transactionIndex];
        if (workerIndex > 6)
        {
          perror("more than 6 works");
        }
        else
        {
          txlog->transaction[transactionIndex].txID = buff->ID;
          txlog->transaction[transactionIndex].worker[workerIndex] = client;
          txlog->transaction[transactionIndex].tstate = TX_INPROGRESS;
          txlog->numWorkersInTransaction[transactionIndex] = workerIndex + 1;
          txlog->transaction[transactionIndex].workersParticipating += 1;
          if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
          {
            perror("Msync problem");
          }
        }
      }
      else if (buff->msgKind == commitRequest)
      {
        int transactionIndex = buff->ID % MAX_TX;
        struct tx *transaction = &(txlog->transaction[transactionIndex]);
        transaction->tstate = TX_VOTING;
        if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
        {
          perror("Msync problem");
        }
        twoPCMssg *response = malloc(sizeof(twoPCMssg));
        bzero(&response, sizeof(twoPCMssg));
        response->ID = buff->ID;
        response->msgKind = prepareToCommit;
        for (int i = 0; i < MAX_WORKERS; i++)
        {
          if ((&transaction->worker[i]) != NULL)
          {
            int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                   sizeof(twoPCMssg), 0, (struct sockaddr *)&transaction->worker[i], sizeof(struct sockaddr_in));
          }
        }
        transaction->start_t = clock();
        if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
        {
          perror("Msync problem");
        }
      }
      else if (buff->msgKind == votingDecision)
      {
        int transactionIndex = buff->ID % MAX_TX;
        struct tx *transaction = &txlog->transaction[transactionIndex];
        if (transaction->tstate == TX_COMMITTED)
        {
          twoPCMssg *response = malloc(sizeof(twoPCMssg));
          bzero(&response, sizeof(twoPCMssg));
          response->ID = buff->ID;
          response->msgKind = commited;
          int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                 sizeof(twoPCMssg), 0, (struct sockaddr *)&client, sizeof(client));
        }
        else if (transaction->tstate == TX_ABORTED || transaction->tstate == TX_Recovering)
        {
          twoPCMssg *response = malloc(sizeof(twoPCMssg));
          bzero(&response, sizeof(twoPCMssg));
          response->ID = buff->ID;
          response->msgKind = aborted;
          int bytesSent = sendto(sockfd, (twoPCMssg *)response,
                                 sizeof(twoPCMssg), 0, (struct sockaddr *)&client, sizeof(client));
        }
      }
      else
      {
        int transactionIndex = buff->ID % MAX_TX;
        struct tx *transaction = &(txlog->transaction[transactionIndex]);
        if (transaction->tstate == TX_VOTING)
        {
          if (buff->msgKind == prepared)
          {
            transaction->preparedVotes += 1;
            if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
            {
              perror("Msync problem");
            }
          }
          else if (buff->msgKind == no)
          {
            transaction->tstate = TX_ABORTED;

            if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
            {
              perror("Msync problem");
            }
            twoPCMssg *response = malloc(sizeof(twoPCMssg));
            bzero(&response, sizeof(twoPCMssg));
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
            free(response);
          }
          if (transaction->preparedVotes == transaction->workersParticipating)
          {
            transaction->tstate = TX_COMMITTED;
            if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE))
            {
              perror("Msync problem");
            }
            twoPCMssg *response = malloc(sizeof(twoPCMssg));
            bzero(&response, sizeof(twoPCMssg));
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
            free(response);
          }
        }
      }
    }
    // for (i = 0;; i = (++i % MAX_WORKERS)) {
    //   struct sockaddr_in client;
    //   socklen_t len;
    //   bzero(&client, sizeof(client));
    //   n = recvfrom(sockfd, buff, sizeof(buff), MSG_WAITALL,
    // 	 (struct sockaddr *) &client, &len);
    //   if (n < 0) {
    //     perror("Receiving error:");
    //     abort();
    //   }
    //   printf("Got a packet\n");
    //   txlog->transaction[i].worker[0] = client;
    //   // Make sure in memory copy is flushed to disk
    //   if (msync(txlog, sizeof(struct transactionSet), MS_SYNC | MS_INVALIDATE)) {
    //     perror("Msync problem");
    //   }

    // }

    // sleep(1000);
  }
}

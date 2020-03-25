
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#include "msg.h"
#include "tworker.h"


void usage(char * cmd) {
  printf("usage: %s  cmdportNum txportNum\n",
	 cmd);
}


int main(int argc, char ** argv) {
  int commandsockfd;
  struct sockaddr_in commandAddr;
  int txnManagersockfd;
  struct sockaddr_in managerAddr;
  unsigned long cmdPort;
  unsigned long txPort;
  // This is some sample code feel free to delete it

  if (argc != 3) {
    usage(argv[0]);
    return -1;
  }

   char * end;
   cmdPort = strtoul(argv[1], &end, 10);
   if (argv[1] == end) {
     printf("Command port conversion error\n");
     exit(-1);
   }
   txPort = strtoul(argv[2], &end, 10);
   if (argv[2] == end) {
     printf("Transaction port conversion error\n");
     exit(-1);
  }
  // Create the socket to receive from the command line

  if ( (commandsockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
  int optval = 1;
  setsockopt(commandsockfd,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));
  // Setup my server information
  memset(&commandAddr, 0, sizeof(commandAddr));
  commandAddr.sin_family = AF_INET;
  commandAddr.sin_port = htons(cmdPort);
  // Accept on any of the machine's IP addresses.
  commandAddr.sin_addr.s_addr = INADDR_ANY;

  // Bind the socket to the requested addresses and port
  if ( bind(commandsockfd, (const struct sockaddr *)&commandAddr,
            sizeof(commandAddr)) < 0 )  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  //create socket to send messages to txn manager
   if ( (txnManagersockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
  setsockopt(txnManagersockfd,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));
  // Setup my server information
  memset(&managerAddr, 0, sizeof(managerAddr));
  managerAddr.sin_family = AF_INET;
  managerAddr.sin_port = htons(txPort);
  // Accept on any of the machine's IP addresses.
  managerAddr.sin_addr.s_addr = INADDR_ANY;

  // Bind the socket to the requested addresses and port
  if ( bind(txnManagersockfd, (const struct sockaddr *)&managerAddr,
            sizeof(managerAddr)) < 0 )  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

   char  logFileName[128];

  /* got the port number create a logfile name */
   snprintf(logFileName, sizeof(logFileName), "TXworker_%u.log", cmdPort);

   int logfileFD;
   
   logfileFD = open(logFileName, O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR );
   if (logfileFD < 0 ) {
     char msg[256];
     snprintf(msg, sizeof(msg), "Opening %s failed", logFileName);
     perror(msg);
     exit(-1);
   }

   // check the logfile size
   struct stat fstatus;
   if (fstat(logfileFD, &fstatus) < 0) {
     perror("Filestat failed");
     exit(-1);
   }

   // Let's see if the logfile has some entries in it by checking the size
   
   if (fstatus.st_size < sizeof(struct logFile)) {
     // Just write out a zeroed file struct
     printf("Initializing the log file size\n");
     struct logFile tx;
     bzero(&tx, sizeof(tx));
    if (write(logfileFD, &tx, sizeof(tx)) != sizeof(tx)) {
      printf("Writing problem to log\n");
      exit(-1);
    }
   }

   // Now map the file in.
   struct logFile  *log = mmap(NULL, 512, PROT_READ | PROT_WRITE, MAP_SHARED, logfileFD, 0);
   if (log == NULL) {
     perror("Log file could not be mapped in:");
     exit(-1);
   }
  
  //if log file is not initialized initiaze it
  //else recover the previous values
  if(!log->initialized){
     log->initialized = 1;
     log->log.txState = WTX_NOTACTIVE;
  } else {
     //recovery phase
     if(log->log.txState == WTX_PREPARED){
       //not sure 
      
     } else if(log->log.txState == WTX_ABORTED || log->log.txState == WTX_BEGIN){
      //rewrite old values to disk
      log->txData.A = log->log.oldA;
      log->txData.B = log->log.oldB;
      strncpy(log->txData.IDstring, log->log.oldIDstring, IDLEN);
      log->log.txState = WTX_TRUNCATE;
      if (msync(log, sizeof(struct logFile), MS_SYNC | MS_INVALIDATE)) {
        perror("Msync problem");
      }

     }else if(log->log.txState == WTX_COMMITTED){
       //rewrite new values to disk 
        log->txData.A = log->log.newA;
        log->txData.B = log->log.newB;
        strncpy(log->txData.IDstring, log->log.newIDstring, IDLEN);
        log->log.txState = WTX_TRUNCATE;
        if (msync(log, sizeof(struct logFile), MS_SYNC | MS_INVALIDATE)) {
          perror("Msync problem");
        }
     }else if(log->log.txState == WTX_TRUNCATE){
       //do nothing
     }
  }

  while(1){
    
    //receiving cmds from cmd.c
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    msgType* cmd = malloc(sizeof(msgType));
    bzero(&cmd, sizeof(msgType));

    int n;
    n = recvfrom(commandsockfd,cmd, sizeof(*cmd), MSG_WAITALL,(struct sockaddr *)&client,&len);
    
    if(n < 0){
      perror("receiving error\n");
    }


    if(cmd->msgID == BEGINTX || cmd->msgID == JOINTX){
      //copy values from disk to log
      log->log.txID = cmd->tid;
      log->log.txState = WTX_BEGIN;
      log->log.transactionManager = managerAddr;
      log->log.oldA = log->txData.A;
      log->log.oldB = log->txData.B;
      strncpy(log->log.oldIDstring, log->txData.IDstring , IDLEN);
      log->log.oldSaved = 1;

      //send msg to manager to begin transaction
      twoPCMssg* buff = malloc(sizeof(twoPCMssg));
      bzero(&buff, sizeof(twoPCMssg));
      buff->ID = cmd->tid;
      buff->msgKind = beginTransaction;
      int bytesSent;
      if (bytesSent = sendto(txnManagersockfd,(twoPCMssg*)buff, sizeof(twoPCMssg),0,
                          (struct sockaddr*)&managerAddr, sizeof(managerAddr)) == -1){
        perror("UDP send failed: ");
      } else {
        printf("success\n");
      }

    }else if(cmd->msgID == NEW_A){

    }else if(cmd->msgID == NEW_B){

    }else if(cmd->msgID == NEW_IDSTR){

    }else if(cmd->msgID == CRASH){

    }else if(cmd->msgID == DELAY_RESPONSE){

    }else if(cmd->msgID == COMMIT){

    }else if(cmd->msgID == COMMIT_CRASH){

    }else if(cmd->msgID == ABORT){

    }else if(cmd->msgID == ABORT_CRASH){

    }else if(cmd->msgID == VOTE_ABORT){

    }




  }
  

   // Some demo data
   strncpy(log->txData.IDstring, "Hi there!! :-)", IDLEN);
   log->txData.A = 10;
   log->txData.B = 100;
   log->log.oldA = 83;
   log->log.newA = 10;
   log->log.oldB = 100;
   log->log.newB = 1023;
   log->initialized = -1;
   if (msync(log, sizeof(struct logFile), MS_SYNC | MS_INVALIDATE)) {
     perror("Msync problem");
   }
   
   
    printf("Command port:  %d\n", cmdPort);
    printf("TX port:       %d\n", txPort);
    printf("Log file name: %s\n", logFileName);
  // Some demo data
   strncpy(log->log.newIDstring, "1234567890123456789012345678901234567890", IDLEN);
  
}

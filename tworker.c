
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
    

  while(1) {
    //if log file is not initialized initiaze it
    //else recover the previous values
   if(!log->initialized){
     log->initialized = 1;
     log->log.txState = WTX_NOTACTIVE;
   } else {
     //recovery phase
     if(log->log.txState == WTX_PREPARED){
       //abort
      
     } else if(log->log.txState == WTX_ABORTED){
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
     }else if(log->log.txState == WTX_BEGIN){
        log->txData.A = log->log.oldA;
        log->txData.B = log->log.oldB;
        strncpy(log->txData.IDstring, log->log.oldIDstring, IDLEN);
        log->log.txState = WTX_TRUNCATE;
        if (msync(log, sizeof(struct logFile), MS_SYNC | MS_INVALIDATE)) {
          perror("Msync problem");
        }

     }
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

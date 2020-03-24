
#ifndef TMANAGER_h
#define TMANGER_h 100
#define MAX_WORKERS 6
#define MAX_TX 4
enum txState {
  TX_NOTINUSE = 100,
  TX_INPROGRESS,
  TX_VOTING,
  TX_ABORTED,
  TX_COMMITTED
};

struct tx {
  unsigned long txID;
  enum txState tstate;
  int preparedVotes;
  int workersParticipating;
  struct  sockaddr_in worker[MAX_WORKERS];
};


struct transactionSet {
  int initialized;
  struct tx transaction[MAX_TX];
  int numWorkersInTransaction[MAX_TX];
};

#endif

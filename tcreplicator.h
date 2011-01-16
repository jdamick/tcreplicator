/*
 * Copyright 2000-2011 NeuStar, Inc. All rights reserved.
 * NeuStar, the Neustar logo and related names and logos are registered
 * trademarks, service marks or tradenames of NeuStar, Inc. All other 
 * product names, company names, marks, logos and symbols may be trademarks
 * of their respective owners.  
 */
#ifndef TCREPLICATOR_TCREPLICATOR_H_
#define TCREPLICATOR_TCREPLICATOR_H_

#include <tculog.h>

enum {
  kTCREPLCTR_LOGDEBUG    = 0,
  kTCREPLCTR_LOGINFO     = 1,
  kTCREPLCTR_LOGERROR    = 2
};

/* logging callback */
typedef void (*LogCallback)(int log_level, const char* msg);


typedef struct { /* type of structure for a replication */
  LogCallback logger;                    // logging calback
  char host[TTADDRBUFSIZ];               // host name
  int port;                              // port number
  char rtspath[PATH_MAX + 1];            // path of the replication time stamp file
  char ulogpath[PATH_MAX + 1];           // path of the update log file
  uint64_t rts;                          // replication time stamp
  int opts;                              // options
  TCADB *adb;                            // database object
  TCULOG *ulog;                          // update log object
  uint32_t sid;                          // server ID number
  bool fail;                             // failure flag
  bool recon;                            // re-connect flag
  bool fatal;                            // fatal error flag
  uint64_t mts;                          // modified time stamp
  bool stop;                             // stop the replicator
  bool async_io;                         // async io (default: true)
  pthread_t thread;
} TCREPLCTR;


/* create a new replicator context */
TCREPLCTR* tcreplctr_new(void);

/* destroy a replicator context and stop all replicator for it */
void tcreplctr_del(TCREPLCTR* replctr);

/* optional server id for the replication */
bool tcreplctr_set_server_id(TCREPLCTR* replctr, uint32_t server_id);

/* optoinal asynchronous io for the replication */
bool tcreplctr_set_async_io(TCREPLCTR* replctr, bool async_io);

/* optoinal update log path */
bool tcreplctr_set_update_log_path(TCREPLCTR* replctr, const char* update_log_path);

/* optoinal timestamp file */
bool tcreplctr_set_timestamp_file(TCREPLCTR* replctr, const char* timestamp_file);

/* optoinal logging callback */
bool tcreplctr_set_log_callback(TCREPLCTR* replctr, LogCallback logger);

/* start replicator from the given host & port to the opened tcadb */
bool tcreplctr_start(TCREPLCTR* replctr, TCADB* tcadb, const char* hostname, uint32_t port);

/* stop replicator */
bool tcreplctr_stop(TCREPLCTR* replctr);
   

#endif

/*
 * Copyright 2000-2011 NeuStar, Inc. All rights reserved.
 * NeuStar, the Neustar logo and related names and logos are registered
 * trademarks, service marks or tradenames of NeuStar, Inc. All other 
 * product names, company names, marks, logos and symbols may be trademarks
 * of their respective owners.  
 */
 
#include "tcreplicator.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <ttutil.h>
#include <tculog.h>
#include <tcrdb.h>


static const int kNUMBUFSIZ      = 32;           // size of a numeric buffer
static const int kTTADDRBUFSIZ   = 1024;         // size of an address buffer
static const int kDEFULIMSIZ     = (1LL<<30);    // default limit size of an update log file


static void* tcreplctr_runslave(void *opq);

static void tcreplctr_log(TCREPLCTR* replctr, int level, const char* format, ...) {
  if (replctr->logger != NULL) {
    const int kOutputBufSize = 2048;
    char out_buf[kOutputBufSize + 1];
    va_list ap;
    va_start(ap, format);
    memset(out_buf, 0, sizeof(out_buf));
    vsnprintf(out_buf, kOutputBufSize, format, ap);
    va_end(ap);
    // hit the callback
    int log_level = kTCREPLCTR_LOGDEBUG;
    if (level == TTLOGDEBUG) {
        log_level = kTCREPLCTR_LOGDEBUG;
    } else if (level == TTLOGINFO) {
      log_level = kTCREPLCTR_LOGINFO;
    } else if (level == TTLOGERROR) {
      log_level = kTCREPLCTR_LOGERROR;
    } else if (level == TTLOGSYSTEM) {
      log_level = kTCREPLCTR_LOGINFO;
    }
    replctr->logger(log_level, out_buf);
  }
}



TCREPLCTR* tcreplctr_new(void) {
  TCREPLCTR* replctr = tcmalloc(sizeof(*replctr));
  memset(replctr->host, 0, sizeof(replctr->host));
  
  replctr->port = 0;
  memset(replctr->rtspath, 0, sizeof(replctr->rtspath));
  strncpy(replctr->rtspath, "/tmp/tcrepl.ts", sizeof(replctr->rtspath));
  memset(replctr->ulogpath, 0, sizeof(replctr->ulogpath));
  replctr->rts = 0;
  
  replctr->opts = 0;
  replctr->opts |= RDBROCHKCON; // enable consistency check on replication file.. 
  
  replctr->adb = NULL;
  replctr->ulog = NULL;
  
  srandom(time(NULL));
  replctr->sid = random();
  replctr->fail = false;
  replctr->recon = false;
  replctr->fatal = false;
  replctr->mts = 0;
  replctr->stop = false;
  replctr->thread = NULL;
  replctr->async_io = true;
  return replctr;
}

void tcreplctr_del(TCREPLCTR* replctr) {
  if (replctr != NULL) {
    tcreplctr_stop(replctr);
    tcfree(replctr);
  }
}

bool tcreplctr_set_server_id(TCREPLCTR* replctr, uint32_t server_id) {
  assert(replctr);
  replctr->sid = server_id;
  return true;
}

bool tcreplctr_set_timestamp_file(TCREPLCTR* replctr, const char* timestamp_file) {
  assert(replctr);
  strncpy(replctr->rtspath, timestamp_file, sizeof(replctr->rtspath));
  return true;
}

bool tcreplctr_set_log_callback(TCREPLCTR* replctr, LogCallback logger) {
  assert(replctr);
  replctr->logger = logger;
  return true;
}

bool tcreplctr_set_async_io(TCREPLCTR* replctr, bool async_io) {
  assert(replctr);
  replctr->async_io = async_io;  
  return true;
}

bool tcreplctr_set_update_log_path(TCREPLCTR* replctr, const char* update_log_path) {
  assert(replctr);
  strncpy(replctr->ulogpath, update_log_path, sizeof(replctr->ulogpath));
  return true;
}

bool tcreplctr_start(TCREPLCTR* replctr,
                     TCADB* tcadb,
                     const char* hostname, 
                     uint32_t port) {

  const uint64_t kUploadLogLimit = kDEFULIMSIZ;
  bool result = true;   
  
  replctr->adb = tcadb;
  
  if (replctr->adb == NULL) {
    tcreplctr_log(replctr, TTLOGERROR, "invalid configuration on Start");
    return false;
  }

  strncpy(replctr->host, hostname, sizeof(replctr->host));
  replctr->port = port;

  replctr->ulog = tculognew();
  if (strlen(replctr->ulogpath) > 0) {
    tcreplctr_log(replctr, TTLOGSYSTEM, "update log configuration: path= %s", replctr->ulogpath);
    if (replctr->async_io && !tculogsetaio(replctr->ulog)) {
     result = false;
     tcreplctr_log(replctr, TTLOGERROR, "tculogsetaio failed");
    }
    if (!tculogopen(replctr->ulog, replctr->ulogpath, kUploadLogLimit)) {
     result = false;
     tcreplctr_log(replctr, TTLOGERROR, "tculogopen failed");
    }
  }
  
  result = pthread_create(&replctr->thread, NULL, tcreplctr_runslave, replctr) == 0;
  if (!result) {
    tcreplctr_log(replctr, TTLOGERROR, "tcreplctr_runslave: startup failed");
  }
  return result;
}


bool tcreplctr_stop(TCREPLCTR* replctr) {
  if (!replctr->stop && !replctr->fail) {
    replctr->stop = true;
    pthread_join(replctr->thread, NULL);
  }
  
  if (replctr->ulog != NULL) {
    if(!tculogclose(replctr->ulog)){
      tcreplctr_log(replctr, TTLOGERROR, "tculogclose failed");
    }
    tculogdel(replctr->ulog);
    replctr->ulog = NULL;
  }
  return true;
}



// This code is based on samples from Tokyo Tyrant.
// this slave thread will replicate from tyrant into our local cabinet.
static void* tcreplctr_runslave(void *opq) {
  TCREPLCTR* replctr = (TCREPLCTR*) opq;
  if (replctr == NULL) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: replicator instance is bad");
    return NULL;
  }

  TCADB *adb = replctr->adb;
  TCULOG *ulog = replctr->ulog;
  uint32_t sid = replctr->sid;

  if (replctr->fatal) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: repl has fatal error");
    return NULL;
  }
  if (replctr->host[0] == '\0' || replctr->port < 1) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: host[%s] or port[%d] is invalid", replctr->host, replctr->port);
    return NULL;
  }
  if (replctr->mts > 0) {
    char rtsbuf[kNUMBUFSIZ];
    int len = sprintf(rtsbuf, "%llu\n", (unsigned long long)replctr->mts);
    if (!tcwritefile(replctr->rtspath, rtsbuf, len))
      tcreplctr_log(replctr, TTLOGERROR, "RunSlave: tcwritefile failed");
    replctr->mts = 0;
  }
  int rtsfd = open(replctr->rtspath, O_RDWR | O_CREAT, 00644);
  if (rtsfd == -1) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: open of rtspath failed");
    return NULL;
  }
  struct stat sbuf;
  if (fstat(rtsfd, &sbuf) == -1) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: stat failed");
    close(rtsfd);
    return NULL;
  }
  char rtsbuf[kNUMBUFSIZ];
  memset(rtsbuf, 0, sizeof(rtsbuf));
  replctr->rts = 0;
  if (sbuf.st_size > 0 && tcread(rtsfd, rtsbuf, tclmin(kNUMBUFSIZ - 1, sbuf.st_size)))
    replctr->rts = tcatoi(rtsbuf);

  // check the database, if it's got 0 records, perhaps something went bad, reset the rts.
  if (tcadbrnum(adb) == 0) {
    replctr->rts = 0;
  }

  TCREPL *repl = tcreplnew();
  pthread_cleanup_push((void (*)(void *))tcrepldel, repl);
  if (tcreplopen(repl, replctr->host, replctr->port, replctr->rts + 1, sid)) {  
    tcreplctr_log(replctr, TTLOGINFO, "slave to master: sid=%u (%s:%d) after %llu",
                 repl->mid, replctr->host, replctr->port, (unsigned long long)replctr->rts);
    replctr->fail = false;
    replctr->recon = false;
    bool error = false;
    uint32_t rsid = 0;
    const char *rbuf = NULL;
    int rsiz = 0;
    uint64_t rts = 0;
    while(!error && !replctr->stop && !replctr->recon &&
          (rbuf = tcreplread(repl, &rsiz, &rts, &rsid)) != NULL) {
      
      if (rsiz < 1) {
        continue; // receive size too small
      }
      bool cc;
      if (!tculogadbredo(adb, rbuf, rsiz, ulog, rsid, repl->mid, &cc)) {
        error = true;
        tcreplctr_log(replctr, TTLOGERROR, "RunSlave: tculogadbredo failed");
      } else if (!cc) {
        if (replctr->opts & RDBROCHKCON) {
          error = true;
          replctr->fatal = true;
          tcreplctr_log(replctr, TTLOGERROR, "RunSlave: detected inconsistency");
        } else {
          tcreplctr_log(replctr, TTLOGINFO, "RunSlave: detected inconsistency");
        }
      }
      if (lseek(rtsfd, 0, SEEK_SET) != -1) {
        int len = sprintf(rtsbuf, "%llu\n", (unsigned long long) rts);
        if (tcwrite(rtsfd, rtsbuf, len)) {
          replctr->rts = rts;
        } else {
          error = true;
          tcreplctr_log(replctr, TTLOGERROR, "RunSlave: tcwrite failed");
        }
      } else {
        error = true;
        tcreplctr_log(replctr, TTLOGERROR, "RunSlave: lseek failed");
      }
      if (!tcadbsync(adb)) {
        error = true;
        tcreplctr_log(replctr, TTLOGERROR, "RunSlave: adb sync failed");
      }
    }
    tcreplclose(repl);
    tcreplctr_log(replctr, TTLOGINFO, "replication finished");
  } else {
    if (!replctr->fail) {
      tcreplctr_log(replctr, TTLOGERROR, "RunSlave: tcreplopen failed");
    }
    replctr->fail = true;
  }
  pthread_cleanup_pop(1);
  if (close(rtsfd) == -1) {
    tcreplctr_log(replctr, TTLOGERROR, "RunSlave: close failed");
  }
  
  return NULL;
}



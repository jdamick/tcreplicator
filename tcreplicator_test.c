/*
 * Copyright 2000-2010 NeuStar, Inc. All rights reserved.
 * NeuStar, the Neustar logo and related names and logos are registered
 * trademarks, service marks or tradenames of NeuStar, Inc. All other 
 * product names, company names, marks, logos and symbols may be trademarks
 * of their respective owners.  
 */
#include "tcreplicator.h"
#include <sys/stat.h>
#include <unistd.h>

void LogIt(int level, const char* msg) {
  printf("Level[%d] %s\n", level, msg);   
}

#define ASSERT_TRUE(res) \
  if (!(res)) { \
    printf("ASSERT FAILED (%d) for: %s is: %s\n", __LINE__, #res, res ? "true" : "false"); exit(1);\
  } else { \
    printf("ASSERT PASSED for: %s is: %s\n", #res, res ? "true" : "false");\
  }

TCADB* CreateADB(const char* db_name) {
  TCADB* adb = tcadbnew();
  if (!tcadbopen(adb, db_name)) {
    tcadbdel(adb);
    adb = NULL;
  }
  return adb;
}

void CloseADB(TCADB* adb) {
  if (adb != NULL) {
    tcadbclose(adb);
    tcadbdel(adb);
  }
}

bool gStopped = false;

void sig_handler(int sig_recv) {
  printf("Recieved SIGINT\n");
  gStopped = true;
}


int main(int argc, const char* argv[]) {
  signal(SIGINT, sig_handler);
  
  mkdir("./tmp", 0755);

  printf("\nTest 1 ---------\n");
  {
    TCREPLCTR* repl = tcreplctr_new();
    ASSERT_TRUE(tcreplctr_set_update_log_path(repl, "./tmp"));
    ASSERT_TRUE(tcreplctr_set_timestamp_file(repl, "./tmp/tcrepl.ts"));
    ASSERT_TRUE(tcreplctr_stop(repl));
    tcreplctr_del(repl);
  }
  
  printf("\nTest 2 ---------\n");
  {
    TCREPLCTR* repl = tcreplctr_new();
    tcreplctr_set_log_callback(repl, LogIt);
    TCADB* db = CreateADB("./tmp/test.tch");

    ASSERT_TRUE(tcreplctr_set_update_log_path(repl, "./tmp"));
    ASSERT_TRUE(tcreplctr_set_timestamp_file(repl, "./tmp/tcrepl.ts"));    
    ASSERT_TRUE(tcreplctr_start(repl, db, "127.0.0.1", 1978));
    tcreplctr_del(repl);
    CloseADB(db);
  }

  printf("\nTest 3 ---------\n");
  {
    TCREPLCTR* repl = tcreplctr_new();
    tcreplctr_set_log_callback(repl, LogIt);
    TCADB* db = CreateADB("./tmp/test.tch");

    ASSERT_TRUE(tcreplctr_set_update_log_path(repl, "./tmp"));
    ASSERT_TRUE(tcreplctr_set_timestamp_file(repl, "./tmp/tcrepl.ts"));    
    ASSERT_TRUE(tcreplctr_start(repl, db, "127.0.0.1", 1978));

    uint64_t num_records_old = 0;
    while (!gStopped) {
      uint64_t num_records = tcadbrnum(db);
      if (num_records_old != num_records) {
        printf("Number of records now: %llu\n", num_records);
        num_records_old = num_records;
      }
      
      usleep(100);
    }
    
    ASSERT_TRUE(tcreplctr_stop(repl));    
    tcreplctr_del(repl);
    CloseADB(db);
  }
  
  
  return 0;
}


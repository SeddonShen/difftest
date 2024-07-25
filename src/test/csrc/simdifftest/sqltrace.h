/***************************************************************************************
* Copyright (c) 2020-2023 Institute of Computing Technology, Chinese Academy of Sciences
* Copyright (c) 2020-2021 Peng Cheng Laboratory
*
* DiffTest is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/
#include <sqlite3.h>

class IoTraceDb{
public:
    IoTraceDb() {

    };

    ~IoTraceDb() {

    }
    // Create the database and tables
    void create_data(const char *file_path);
    // Insert data of the column of the id
    void alter_table(int size, const char *name, const int name_len, const char *value_str, const int value_len);
    // Display open things, batch insert data
    void insert_batch(const char *name, const int name_len, const char *value_str, const int value_len);
    // Closing the database
    void close() {
        sqlite3_close(conn);    
        return;
    }

    void faile_exit(char *cause);
    // Find ID the exists
    int id_exists(int id);
    // Update head value
    void update_head(int value) {
        head = value;
    }
    // Clear the database tables
    void drop();

private:
    sqlite3 *conn = nullptr;
    int rc;
    int head = 0;
};

int sql_init(char *file_path);

enum DIFFSTATE_PERF {
  RefillEvent,
  L1TLBEvent,
  InstrCommit,
  LoadEvent,
  TrapEvent,
  ArchIntRegState,
  ArchFpRegState,
  ArchVecRegState,
  ArchEvent,
  CSRState,
  HCSRState,
  DebugMode,
  VecCSRState,
  IntWriteback,
  FpWriteback,
  VecWriteback,
  L2TLBEvent,
  AtomicEvent,
  LrScEvent,
  SbufferEvent,
  StoreEvent,
  DIFFSTATE_PERF_NUM
};

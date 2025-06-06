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

#include "device.h"
#ifndef CONFIG_NO_DIFFTEST
#include "difftest.h"
#endif // CONFIG_NO_DIFFTEST
#include "flash.h"
#ifndef CONFIG_NO_DIFFTEST
#include "goldenmem.h"
#endif // CONFIG_NO_DIFFTEST
#include "ram.h"
#ifndef CONFIG_NO_DIFFTEST
#include "refproxy.h"
#endif // CONFIG_NO_DIFFTEST
#include <common.h>
#include <locale.h>
#ifdef CONFIG_DIFFTEST_DEFERRED_RESULT
#include "svdpi.h"
#endif // CONFIG_DIFFTEST_DEFERRED_RESULT
#ifdef CONFIG_DIFFTEST_PERFCNT
#include "perf.h"
#endif // CONFIG_DIFFTEST_PERFCNT
#include "remote_bitbang.h"

static bool has_reset = false;
static char bin_file[256] = "/dev/zero";
static char *flash_bin_file = NULL;
static char *gcpt_restore_bin = NULL;
static bool enable_difftest = true;
static uint64_t max_instrs = 0;
static char *workload_list = NULL;
static uint64_t overwrite_nbytes = 0xe00;
struct core_end_info_t {
  bool core_trap[NUM_CORES];
  double core_cpi[NUM_CORES];
  uint8_t core_trap_num;
};
static core_end_info_t core_end_info;

enum {
  SIMV_RUN,
  SIMV_DONE,
  SIMV_FAIL,
} simv_state;

extern "C" void set_bin_file(char *s) {
  printf("ram image:%s\n", s);
  strcpy(bin_file, s);
}

extern "C" void set_flash_bin(char *s) {
  printf("flash image:%s\n", s);
  flash_bin_file = (char *)malloc(256);
  strcpy(flash_bin_file, s);
}

extern "C" void set_overwrite_nbytes(uint64_t len) {
  overwrite_nbytes = len;
}

extern "C" void set_gcpt_bin(char *s) {
  gcpt_restore_bin = (char *)malloc(256);
  strcpy(gcpt_restore_bin, s);
}

extern "C" void set_max_instrs(uint64_t mc) {
  printf("set max instrs: %lu\n", mc);
  max_instrs = mc;
}

extern "C" uint64_t get_stuck_limit() {
#ifdef CONFIG_NO_DIFFTEST
  return 0;
#else
  return Difftest::stuck_limit;
#endif // CONFIG_NO_DIFFTEST
}

#ifndef CONFIG_NO_DIFFTEST
extern const char *difftest_ref_so;
extern "C" void set_diff_ref_so(char *s) {
  printf("diff-test ref so:%s\n", s);
  char *buf = (char *)malloc(256);
  strcpy(buf, s);
  difftest_ref_so = buf;
}
#else
extern "C" void set_diff_ref_so(char *s) {
  printf("difftest is not enabled. +diff=%s is ignore.\n", s);
}
#endif // CONFIG_NO_DIFFTEST

extern "C" void set_workload_list(char *s) {
  workload_list = (char *)malloc(256);
  strcpy(workload_list, s);
  printf("set workload list %s \n", workload_list);
}

bool switch_workload_completed = false;
int switch_workload() {
  static FILE *fp = fopen(workload_list, "r");
  if (fp) {
    char name[128];
    int num;
    if (fscanf(fp, "%s %d", name, &num) == 2) {
      set_bin_file(name);
      set_max_instrs(num);
    } else if (feof(fp)) {
      printf("Workload list is completed\n");
      switch_workload_completed = true;
      fclose(fp);
      return 1;
    } else {
      printf("Unknown workload list format\n");
      fclose(fp);
      return 1;
    }
  } else {
    printf("Fail to open workload list %s\n", workload_list);
    return 1;
  }
  return 0;
}

extern "C" bool workload_list_completed() {
  return switch_workload_completed;
}

extern "C" void set_no_diff() {
  printf("disable diff-test\n");
  enable_difftest = false;
}

extern "C" void set_simjtag() {
  enable_simjtag = true;
}

extern "C" uint8_t simv_init() {
  if (workload_list != NULL) {
    if (switch_workload())
      return 1;
  }
  common_init("simv");

  init_ram(bin_file, DEFAULT_EMU_RAM_SIZE);
#ifdef WITH_DRAMSIM3
  dramsim3_init(nullptr);
#endif
  if (gcpt_restore_bin != NULL) {
    overwrite_ram(gcpt_restore_bin, overwrite_nbytes);
  }
  init_flash(flash_bin_file);

#ifndef CONFIG_NO_DIFFTEST
  difftest_init();
#endif // CONFIG_NO_DIFFTEST

  init_device();

#ifndef CONFIG_NO_DIFFTEST
  if (enable_difftest) {
    init_goldenmem();
    init_nemuproxy(DEFAULT_EMU_RAM_SIZE);
  }
#endif // CONFIG_NO_DIFFTEST

  return 0;
}

#ifdef OUTPUT_CPI_TO_FILE
int output_cpi_to_file() {
  FILE *d2q_fifo = fopen(OUTPUT_CPI_TO_FILE, "w+");
  if (d2q_fifo == NULL) {
    printf("open dq2_fifo fail\n");
    return SIMV_FAIL;
  }
  printf("OUTPUT_CPI_TO_FIFO to %s\n", OUTPUT_CPI_TO_FILE);
  for (size_t i = 0; i < NUM_CORES; i++) {
    fprintf(d2q_fifo, "%d,%.6lf\n", i, core_end_info.core_cpi[i]);
  }
  fclose(d2q_fifo);
  return 0;
}
#endif

extern "C" uint8_t simv_step() {
  if (assert_count > 0) {
    return SIMV_FAIL;
  }

#ifndef CONFIG_NO_DIFFTEST
  if (enable_difftest) {
    if (difftest_step())
      return SIMV_FAIL;
  } else {
    difftest_set_dut();
  }

  if (difftest_state() != -1) {
    int trapCode = difftest_state();
    for (int i = 0; i < NUM_CORES; i++) {
      printf("Core %d: ", i);
      uint64_t pc = difftest[i]->get_trap_event()->pc;
      switch (trapCode) {
        case 0: eprintf(ANSI_COLOR_GREEN "HIT GOOD TRAP at pc = 0x%" PRIx64 "\n" ANSI_COLOR_RESET, pc); break;
        default: eprintf(ANSI_COLOR_RED "Unknown trap code: %d\n" ANSI_COLOR_RESET, trapCode);
      }
      difftest[i]->display_stats();
    }
    if (trapCode == 0)
      return SIMV_DONE;
    else
      return SIMV_FAIL;
  }

  if (max_instrs != 0) { // 0 for no limit
    for (int i = 0; i < NUM_CORES; i++) {
      if (core_end_info.core_trap[i])
        continue;
      auto trap = difftest[i]->get_trap_event();
      if (max_instrs < trap->instrCnt) {
        core_end_info.core_trap[i] = true;
        core_end_info.core_trap_num++;
        eprintf(ANSI_COLOR_GREEN "EXCEEDED CORE-%d MAX INSTR: %ld\n" ANSI_COLOR_RESET, i, max_instrs);
        difftest[i]->display_stats();
        core_end_info.core_cpi[i] = (double)trap->cycleCnt / (double)trap->instrCnt;
        if (core_end_info.core_trap_num == NUM_CORES) {
#ifdef OUTPUT_CPI_TO_FILE
          if (output_cpi_to_file())
            return SIMV_FAIL;
#endif
          return SIMV_DONE;
        }
      }
    }
  }
#endif // CONFIG_NO_DIFFTEST
  return 0;
}

#ifdef CONFIG_DIFFTEST_DEFERRED_RESULT
svScope deferredResultScope;
extern "C" void set_deferred_result_scope();
void set_deferred_result_scope() {
  deferredResultScope = svGetScope();
}

extern "C" void set_deferred_result(uint8_t result);
void difftest_deferred_result(uint8_t result) {
  if (deferredResultScope == NULL) {
    printf("Error: Could not retrieve deferred result scope, set first\n");
    assert(deferredResultScope);
  }
  svSetScope(deferredResultScope);
  set_deferred_result(result);
}
#endif // CONFIG_DIFFTEST_DEFERRED_RESULT

#ifdef WITH_DRAMSIM3
extern "C" void simv_tick() {
  dramsim3_step();
}
#endif

void simv_finish() {
  common_finish();
  flash_finish();

#ifndef CONFIG_NO_DIFFTEST
  difftest_finish();
  if (enable_difftest) {
    goldenmem_finish();
  }
#endif // CONFIG_NO_DIFFTEST

  finish_device();
  delete simMemory;
  simMemory = nullptr;

  for (int i = 0; i < NUM_CORES; i++)
    core_end_info.core_trap[i] = false;
  core_end_info.core_trap_num = 0;
}

static uint8_t simv_result = SIMV_RUN;
#ifdef CONFIG_DIFFTEST_DEFERRED_RESULT
extern "C" void simv_nstep(uint8_t step) {
  if (simv_result == SIMV_FAIL || difftest == NULL)
    return;
#else
extern "C" uint8_t simv_nstep(uint8_t step) {
#ifndef CONFIG_NO_DIFFTEST
  if (difftest == NULL)
    return 0;
#endif // CONFIG_NO_DIFFTEST
#endif // CONFIG_DIFFTEST_DEFERRED_RESULT

#ifdef CONFIG_DIFFTEST_PERFCNT
#ifndef CONFIG_DIFFTEST_INTERNAL_STEP
  difftest_calls[perf_simv_nstep]++;
  difftest_bytes[perf_simv_nstep] += 1;
#endif // CONFIG_DIFFTEST_INTERNAL_STEP
#endif // CONFIG_DIFFTEST_PERFCNT

#ifndef CONFIG_NO_DIFFTEST
  difftest_switch_zone();
#endif // CONFIG_NO_DIFFTEST

  for (int i = 0; i < step; i++) {
    int ret = simv_step();
    if (ret) {
      simv_result = ret;
      simv_finish();
#ifdef CONFIG_DIFFTEST_DEFERRED_RESULT
      difftest_deferred_result(ret);
      return;
#else
      return ret;
#endif // CONFIG_DIFFTEST_DEFERRED_RESULT
    }
  }
#ifndef CONFIG_DIFFTEST_DEFERRED_RESULT
  return 0;
#endif // CONFIG_DIFFTEST_DEFERRED_RESULT
}

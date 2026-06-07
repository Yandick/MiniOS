CC = gcc
PYTHON ?= python3
MKDIR_P = mkdir -p $@
CLEAN_BUILD = rm -rf $(BUILD_DIR)
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude -pthread
LDFLAGS ?= -pthread
BUILD_DIR := build
EVIDENCE_DIR := $(BUILD_DIR)/evidence
SRC_DIR := src
TARGET := $(BUILD_DIR)/os_project
TEST_TARGET := $(BUILD_DIR)/test_core
CORE_SRC := $(SRC_DIR)/utils.c $(SRC_DIR)/scheduling.c $(SRC_DIR)/realtime.c $(SRC_DIR)/memory.c $(SRC_DIR)/sync_demo.c $(SRC_DIR)/tinyfs.c $(SRC_DIR)/benchmark.c $(SRC_DIR)/kernel.c
APP_SRC := $(SRC_DIR)/main.c $(CORE_SRC)
TEST_SRC := tests/test_core.c $(CORE_SRC)
RUN_TARGET := ./$(TARGET)
RUN_TEST_TARGET := ./$(TEST_TARGET)

.PHONY: all test check evidence home workbench demo tour clean

all: $(TARGET)

$(BUILD_DIR):
	$(MKDIR_P)

$(EVIDENCE_DIR): | $(BUILD_DIR)
	$(MKDIR_P)

$(TARGET): $(APP_SRC) include/os_project.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(APP_SRC) -o $@ $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC) include/os_project.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_TARGET)
	$(RUN_TEST_TARGET)

check: test

evidence: $(TARGET) $(TEST_TARGET) | $(EVIDENCE_DIR)
	$(RUN_TEST_TARGET) > $(EVIDENCE_DIR)/core_tests.txt
	$(RUN_TARGET) tour --script data/tour_script.txt > $(EVIDENCE_DIR)/tour_default.log
	$(RUN_TARGET) tour --mode full > $(EVIDENCE_DIR)/full_demo.log
	$(RUN_TARGET) tour --script data/evidence_help.txt > $(EVIDENCE_DIR)/help.txt
	$(RUN_TARGET) tour --script data/evidence_tracebench.txt > $(EVIDENCE_DIR)/tracebench.log
	$(RUN_TARGET) tour --script data/evidence_benchmark_subset.txt > $(EVIDENCE_DIR)/benchmark_subset.log
	$(RUN_TARGET) tour --script data/evidence_autotune.txt > $(EVIDENCE_DIR)/autotune.log
	$(RUN_TARGET) tour --script data/evidence_performance_export.txt > $(EVIDENCE_DIR)/performance_export.log
	$(RUN_TARGET) tour --script data/evidence_tinyfs_fd_smoke.txt > $(EVIDENCE_DIR)/tinyfs_fd_smoke.log
	$(PYTHON) scripts/export_demo_assets.py

home: $(TARGET)
	$(PYTHON) scripts/workbench_server.py --port 8000 --start-path /

workbench: $(TARGET)
	$(PYTHON) scripts/workbench_server.py --port 8001 --start-path /workbench.html

demo: $(TARGET)
	$(PYTHON) scripts/workbench_server.py --port 8002 --start-path "/minios_story.html?mode=full"

tour: $(TARGET)
	$(RUN_TARGET) tour --script data/tour_script.txt

clean:
	$(CLEAN_BUILD)

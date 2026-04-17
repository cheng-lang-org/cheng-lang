CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

OUT_DIR := ../artifacts/bootstrap
BOOTSTRAP_OUT := $(OUT_DIR)/cheng_v2_bootstrap
COMPILER_OUT := $(OUT_DIR)/cheng_v2c

.PHONY: all clean retired selfhost tooling-selfhost program-selfhost compiler-core-release compiler-core-system-link compiler-core-system-link-exec obj-file-layout lsmr-contracts full-selfhost

retired:
	@echo "v2 bootstrap retired; use v3 instead" 1>&2
	@exit 1

all clean selfhost tooling-selfhost program-selfhost compiler-core-release compiler-core-system-link compiler-core-system-link-exec obj-file-layout lsmr-contracts full-selfhost $(BOOTSTRAP_OUT) $(COMPILER_OUT): retired

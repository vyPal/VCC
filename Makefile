CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror
SRC = $(wildcard src/*.c)
BUILD_DIR = ./build

ifeq (run,$(firstword $(MAKECMDGOALS)))
  RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval .PHONY: $(RUN_ARGS))
  $(eval $(RUN_ARGS):;@:)
endif

.PHONY: all run clean init

all: init $(SRC)
	@$(CC) $(SRC) $(CFLAGS) -o $(BUILD_DIR)/vcc

init:
	@mkdir -p $(BUILD_DIR)

run: all
	@./$(BUILD_DIR)/vcc $(RUN_ARGS)

clean:
	@rm -rf $(BUILD_DIR)

%:
	@:

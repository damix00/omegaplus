CC := cc
CFLAGS := -std=c11 -Wall -Wextra -pedantic -Iinclude

SRC_DIR := src
BIN_DIR := bin
BUILD_DIR := build
EXAMPLES_DIR := examples

COMPILER := $(BIN_DIR)/omegac
COMPILER_SOURCES := $(shell find $(SRC_DIR) -type f -name '*.c' | sort)
FIB_EXAMPLE_SRC := $(EXAMPLES_DIR)/fib_recursive.u
FLOAT_EXAMPLE_SRC := $(EXAMPLES_DIR)/float_demo.u
VECTOR_EXAMPLE_SRC := $(EXAMPLES_DIR)/vector_demo.u
FIB_EXAMPLE_BIN := $(BUILD_DIR)/fib_recursive
FLOAT_EXAMPLE_BIN := $(BUILD_DIR)/float_demo
VECTOR_EXAMPLE_BIN := $(BUILD_DIR)/vector_demo
MATH_EXAMPLE_SRC := $(EXAMPLES_DIR)/math_demo.u
MATH_EXAMPLE_BIN := $(BUILD_DIR)/math_demo

.PHONY: all clean test example run-example float-example run-float-example vector-example run-vector-example math-example run-math-example

all: $(COMPILER)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(COMPILER): $(COMPILER_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(COMPILER_SOURCES) -o $(COMPILER)

example: $(COMPILER) $(FIB_EXAMPLE_SRC) | $(BUILD_DIR)
	$(COMPILER) $(FIB_EXAMPLE_SRC) -o $(FIB_EXAMPLE_BIN)

run-example: example
	$(FIB_EXAMPLE_BIN)

float-example: $(COMPILER) $(FLOAT_EXAMPLE_SRC) | $(BUILD_DIR)
	$(COMPILER) $(FLOAT_EXAMPLE_SRC) -o $(FLOAT_EXAMPLE_BIN)

run-float-example: float-example
	$(FLOAT_EXAMPLE_BIN)

vector-example: $(COMPILER) $(VECTOR_EXAMPLE_SRC) | $(BUILD_DIR)
	$(COMPILER) $(VECTOR_EXAMPLE_SRC) -o $(VECTOR_EXAMPLE_BIN)

run-vector-example: vector-example
	$(VECTOR_EXAMPLE_BIN)

math-example: $(COMPILER) $(MATH_EXAMPLE_SRC) | $(BUILD_DIR)
	$(COMPILER) $(MATH_EXAMPLE_SRC) -o $(MATH_EXAMPLE_BIN)

run-math-example: math-example
	$(MATH_EXAMPLE_BIN)

test: $(COMPILER)
	./tests/run_tests.sh

clean:
	rm -f $(COMPILER)
	rm -rf $(BUILD_DIR)/*
	rm -rf tests/tmp
	rm -f omegac
	rm -f example example.o example.s

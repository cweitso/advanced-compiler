LLVM_CONFIG ?= /home/DannyTso/advanced_compiler/llvm_build/bin/llvm-config

CC=`$(LLVM_CONFIG) --bindir`/clang
CXX=`$(LLVM_CONFIG) --bindir`/clang++
CXXFLAGS=`$(LLVM_CONFIG) --cppflags` -fPIC -fno-rtti
LDFLAGS=`$(LLVM_CONFIG) --ldflags`
IRFLAGS=-Xclang -disable-O0-optnone -fno-discard-value-names -S -emit-llvm
OPT=`$(LLVM_CONFIG) --bindir`/opt

SOURCE_FILE ?= test1.c
IR_FILE = $(SOURCE_FILE:.c=.ll)

.PHONY: all test run clean test1 test2

all: hw1.so test

test: $(IR_FILE)

hw1.so: hw1.cpp
	$(CXX) -shared -o $@ $< $(CXXFLAGS) $(LDFLAGS)

$(IR_FILE): $(SOURCE_FILE)
	$(CC) $(IRFLAGS) -o $@ $<
	$(OPT) -S -passes=mem2reg,loop-rotate,loop-simplify $@ -o $@

run: $(IR_FILE) hw1.so
	$(OPT) -disable-output -load-pass-plugin=./hw1.so -passes=hw1 $<

# Convenience targets for test cases
test1: hw1.so
	$(MAKE) SOURCE_FILE=test1.c test
	$(MAKE) SOURCE_FILE=test1.c run

test2: hw1.so
	$(MAKE) SOURCE_FILE=test2.c test
	$(MAKE) SOURCE_FILE=test2.c run

# Check results against answers
check1: test1
	@if [ -f hw1_checker.py ] && [ -f test1_answer.json ]; then \
		python3 hw1_checker.py test1_answer.json test1.json; \
	else \
		echo "Missing hw1_checker.py or test1_answer.json"; \
	fi

check2: test2
	@if [ -f hw1_checker.py ] && [ -f test2_answer.json ]; then \
		python3 hw1_checker.py test2_answer.json test2.json; \
	else \
		echo "Missing hw1_checker.py or test2_answer.json"; \
	fi

clean:
	rm -f *.o *.ll *.so *test1.json *test2.json
# Advanced Compiler 2025 - HW1 Data Dependence Analysis

## Homework 1 - Data Dependence Analysis

In this project, we will use the LLVM intermediate representation to perform data dependence analysis on C programs with for-loops. You need to report the dependences within the for loops. The data dependences in this project only concern dependences between array variables; you do not need to report dependences for scalar variables.

### Case 1 - test1.c

This case considers the array index format i + c for one-dimensional arrays and a single-level loop. The array is accessed as A[f(i)], where f(i) is defined as i + c, with i representing the index and c being a constant.

- test1.c

```
//test1.c
int main(){
    int i;
    int A[20], B[20], C[20];
    for (i = 4; i < 20; i++) {
        A[i] = C[i];
        B[i] = A[i - 4];
    }
    return 0;
}

```

- Unfolding statements example
    
    i=4:S14 A[4]=C[4]S24 B[4]=A[0]i=5:S15 A[5]=C[5]S25 B[5]=A[1]i=6:S16 A[6]=C[6]S26 B[6]=A[2]i=7:S17 A[7]=C[7]S27 B[7]=A[3]i=8:S18 A[8]=C[8]S28 B[8]=A[4]i=9:S19 A[9]=C[9]S29 B[9]=A[5]...
    
- Output (Human-readable version)

```bash
====Flow Dependence====
(i=4,i=8)
A:S1 -----> S2
(i=5,i=9)
A:S1 -----> S2
(i=6,i=10)
A:S1 -----> S2
(i=7,i=11)
A:S1 -----> S2
(i=8,i=12)
A:S1 -----> S2
(i=9,i=13)
A:S1 -----> S2
(i=10,i=14)
A:S1 -----> S2
(i=11,i=15)
A:S1 -----> S2
(i=12,i=16)
A:S1 -----> S2
(i=13,i=17)
A:S1 -----> S2
(i=14,i=18)
A:S1 -----> S2
(i=15,i=19)
A:S1 -----> S2
====Anti-Dependence====
====Output Dependence====

```

- Output (json format for hw1)

```
{
  "FlowDependence": [
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 4,
      "dst_stmt": 2,
      "dst_idx": 8
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 5,
      "dst_stmt": 2,
      "dst_idx": 9
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 6,
      "dst_stmt": 2,
      "dst_idx": 10
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 7,
      "dst_stmt": 2,
      "dst_idx": 11
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 8,
      "dst_stmt": 2,
      "dst_idx": 12
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 9,
      "dst_stmt": 2,
      "dst_idx": 13
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 10,
      "dst_stmt": 2,
      "dst_idx": 14
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 11,
      "dst_stmt": 2,
      "dst_idx": 15
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 12,
      "dst_stmt": 2,
      "dst_idx": 16
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 13,
      "dst_stmt": 2,
      "dst_idx": 17
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 14,
      "dst_stmt": 2,
      "dst_idx": 18
    },
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 15,
      "dst_stmt": 2,
      "dst_idx": 19
    }
  ],
  "AntiDependence": [],
  "OutputDependence": []
}

```

### Case 2 - test2.c

In this case, we consider an array variable with a subscript in the form of c * i + d for one-dimensional arrays and a single-level loop. The format is A[f(i)], where f(i) is defined as c * i + d.

- test2.c:

```
//test2.c
int main(){
    int i;
    int A[40], C[40], D[40];
    for (i = 2; i < 20; i++) {
        A[i] = C[i];
        D[i] = A[3 * i - 4];
        D[i - 1] = C[2 * i];
    }
    return 0;
}

```

- Unfolding statements example
    
    i=2:S12 A[2]=C[2]S22 D[2]=A[2]S32 D[1]=C[4]i=3:S13 A[3]=C[3]S23 D[3]=A[5]S33 D[2]=C[6]i=4:S14 A[4]=C[4]S24 D[4]=A[8]S34 D[3]=C[8]i=5:S15 A[5]=C[5]S25 D[5]=A[11]S35 D[4]=C[10]...
    
- Output (Human-readable version)

```bash
====Flow Dependence====
(i=2,i=2)
A:S1 -----> S2
====Anti-Dependence====
(i=3,i=5)
A:S2 --A--> S1
(i=4,i=8)
A:S2 --A--> S1
(i=5,i=11)
A:S2 --A--> S1
(i=6,i=14)
A:S2 --A--> S1
(i=7,i=17)
A:S2 --A--> S1
====Output Dependence====
(i=2,i=3)
D:S2 --O--> S3
(i=3,i=4)
D:S2 --O--> S3
(i=4,i=5)
D:S2 --O--> S3
(i=5,i=6)
D:S2 --O--> S3
(i=6,i=7)
D:S2 --O--> S3
(i=7,i=8)
D:S2 --O--> S3
(i=8,i=9)
D:S2 --O--> S3
(i=9,i=10)
D:S2 --O--> S3
(i=10,i=11)
D:S2 --O--> S3
(i=11,i=12)
D:S2 --O--> S3
(i=12,i=13)
D:S2 --O--> S3
(i=13,i=14)
D:S2 --O--> S3
(i=14,i=15)
D:S2 --O--> S3
(i=15,i=16)
D:S2 --O--> S3
(i=16,i=17)
D:S2 --O--> S3
(i=17,i=18)
D:S2 --O--> S3
(i=18,i=19)
D:S2 --O--> S3

```

- Output (json format for hw2)

```
{
  "FlowDependence": [
    {
      "array": "A",
      "src_stmt": 1,
      "src_idx": 2,
      "dst_stmt": 2,
      "dst_idx": 2
    }
  ],
  "AntiDependence": [
    {
      "array": "A",
      "src_stmt": 2,
      "src_idx": 3,
      "dst_stmt": 1,
      "dst_idx": 5
    },
    {
      "array": "A",
      "src_stmt": 2,
      "src_idx": 4,
      "dst_stmt": 1,
      "dst_idx": 8
    },
    {
      "array": "A",
      "src_stmt": 2,
      "src_idx": 5,
      "dst_stmt": 1,
      "dst_idx": 11
    },
    {
      "array": "A",
      "src_stmt": 2,
      "src_idx": 6,
      "dst_stmt": 1,
      "dst_idx": 14
    },
    {
      "array": "A",
      "src_stmt": 2,
      "src_idx": 7,
      "dst_stmt": 1,
      "dst_idx": 17
    }
  ],
  "OutputDependence": [
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 2,
      "dst_stmt": 3,
      "dst_idx": 3
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 3,
      "dst_stmt": 3,
      "dst_idx": 4
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 4,
      "dst_stmt": 3,
      "dst_idx": 5
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 5,
      "dst_stmt": 3,
      "dst_idx": 6
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 6,
      "dst_stmt": 3,
      "dst_idx": 7
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 7,
      "dst_stmt": 3,
      "dst_idx": 8
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 8,
      "dst_stmt": 3,
      "dst_idx": 9
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 9,
      "dst_stmt": 3,
      "dst_idx": 10
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 10,
      "dst_stmt": 3,
      "dst_idx": 11
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 11,
      "dst_stmt": 3,
      "dst_idx": 12
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 12,
      "dst_stmt": 3,
      "dst_idx": 13
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 13,
      "dst_stmt": 3,
      "dst_idx": 14
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 14,
      "dst_stmt": 3,
      "dst_idx": 15
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 15,
      "dst_stmt": 3,
      "dst_idx": 16
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 16,
      "dst_stmt": 3,
      "dst_idx": 17
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 17,
      "dst_stmt": 3,
      "dst_idx": 18
    },
    {
      "array": "D",
      "src_stmt": 2,
      "src_idx": 18,
      "dst_stmt": 3,
      "dst_idx": 19
    }
  ]
}

```

### Hidden Test Cases

- The hidden test cases will be variations of the provided test1 and test2 cases, with different loop bounds and subscripts.
- The subscripts will only be in the format 'i + c' and 'c * i + d'.

### Output Format

- The output should be in JSON format, as shown in the test1 and test2 output.
    - File name: input_name.json (e.g., if the input is test1.c/ll, the output file name should be test1.json).
- The order of dependences is irrelevant.

## Brief Introduction to LLVM

- [The LLVM Compiler Infrastructure](https://llvm.org/)([http://www.aosabook.org/en/llvm.html](http://www.aosabook.org/en/llvm.html))
    
    ![](https://i.imgur.com/1t0hlWi.png)
    
    ![](https://i.imgur.com/fQt8c90.png)
    

### LLVM IR Overview

- Low level assembly like language
- Register machine, infinite number of registers
- Each instruction defines a new register (SSA form)
- Load/Store Architecture
- [LLVM Language Reference Manual](http://llvm.org/docs/LangRef.html)

### LLVM Program Structure

### C Program

```
// foo.c
int foo(int a, int b) {
    int sum = a + b;
    if (sum < 0) sum = 0;
    return sum;
}

```

### LLVM IR

![](https://hackmd.io/_uploads/BkWy_i4Zp.png)

- LLVM Modules
    - An LLVM program consists of one or more modules, each representing a translation unit or a collection of source files. A module contains various entities, such as functions, global variables, and type definitions.
- Functions
    - Functions in LLVM IR are similar to functions in high-level languages. Each function comprises basic blocks that contain a sequence of instructions.
- Basic Blocks
    - Basic blocks are sequences of instructions with a single entry point and a single exit point. They are essential for control flow analysis and optimization.
- Instructions
    - Instructions are the fundamental building blocks of LLVM IR. Each instruction performs a specific operation, such as arithmetic, memory access, or control flow.

## How to Write an LLVM Pass

### Hello Pass

The following sample pass will invoke the run() method every time it encounters a function inside an LLVM module, printing out the name of the function.

### hw1 pass sample (hw1.cpp)

```
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

namespace {

struct HW1Pass : public PassInfoMixin<HW1Pass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

PreservedAnalyses HW1Pass::run(Function &F, FunctionAnalysisManager &FAM) {
  errs() << "[HW1]: " << F.getName() << '\n';
  return PreservedAnalyses::all();
}

} // end anonymous namespace

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HW1Pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "hw1") {
                    FPM.addPass(HW1Pass());
                    return true;
                  }
                  return false;
                });
          }};
}

```

### Iterate over LLVM IR

```
for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
        processInst(I);
    }
}

```

### Check pointer type

```
for (Instruction &I : BB) {
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
        processLoadInst(LI);
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        processStoreInst(SI);
    }
}

```

### Get Loop Information by LoopAnalysis Pass (Optional)

```
auto &LI = FAM.getResult<LoopAnalysis>(F);
for (const auto &L : LI) {
    processLoop(L);
}

```

- [llvm::Loop Class Reference](https://llvm.org/doxygen/classllvm_1_1Loop.html)
- Hint: getBounds()

### Navigate through LLVM API Reference

- [LLVM API Reference](https://llvm.org/doxygen/)
- For example, if you want to know how to get the pointer operand from a LoadInst:
    1. Search "LLVM LoadInst" on your search engine and go to the API documentation.
        
        ![](https://hackmd.io/_uploads/rJp7LGSWa.png)
        
    2. See whether its Public Member Functions list has the method you want to use.There is a getPointerOperand() function that seems to work!
        
        ![](https://hackmd.io/_uploads/HkF6UMSba.png)
        
        ![](https://hackmd.io/_uploads/S1R0wfrZp.png)
        
    3. You can also try to search its base/derived class.
        
        ![](https://hackmd.io/_uploads/Bkx4VDfr-a.png)
        
- Another tip for finding out how to use an API is to search the LLVM codebase and observe how others uesd it.
    
    ```bash
    # In llvm-project-17.0.2.src/llvm
    $ grep -r LoadInst
    
    ```
    

### Run the Pass on LLVM IR Using opt

![](https://i.imgur.com/xrK0Dkw.png)

```bash
$ opt -S -load-pass-plugin=./mypass.so -passes=mypass-name input.ll -o output.ll

```

## Homework 1 - Development Environment

### Download llvm-project Source

- using curl

```bash
curl -L -O https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.2/llvm-project-17.0.2.src.tar.xz

```

- using wget

```bash
$ wget https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.2/llvm-project-17.0.2.src.tar.xz

```

### Extract Files from the Archive

```bash
$ tar -xf llvm-project-17.0.2.src.tar.xz

```

### Build Clang and LLVM

- Create a build directory (select your host target for -DLLVM_TARGETS_TO_BUILD)

```bash
$ mkdir llvm_build && cd llvm_build
$ cmake ../llvm-project-17.0.2.src/llvm \
    -DLLVM_ENABLE_PROJECTS="clang" \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DCMAKE_BUILD_TYPE=Release

```

- Optional: Build LLVM with Ninja (alternative for line 2)

```bash
$ cmake -G Ninja ../llvm-project-17.0.2.src/llvm \
    -DLLVM_ENABLE_PROJECTS="clang" \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DCMAKE_BUILD_TYPE=Release

```

- Start the build in the build directory

```bash
$ cmake --build .

```

### Create hw1 Directory

- Your directory structure may look like:

```
advanced_compiler/
|-- hw1/
|   |-- hw1.cpp
|   |-- Makefile
|   |-- test1.c
|   |-- test2.c
|-- llvm-project-17.0.2.src/
|-- llvm_build/

```

### Makefile Sample

```makefile
LLVM_CONFIG ?= /path/to/your/llvm_build/bin/llvm-config

CXX=`$(LLVM_CONFIG) --bindir`/clang
CXXFLAGS=`$(LLVM_CONFIG) --cppflags` -fPIC -fno-rtti
LDFLAGS=`$(LLVM_CONFIG) --ldflags`
IRFLAGS=-Xclang -disable-O0-optnone -fno-discard-value-names -S -emit-llvm
OPT=`$(LLVM_CONFIG) --bindir`/opt

SOURCE_FILE ?= test1.c
IR_FILE = $(SOURCE_FILE:.c=.ll)

.PHONY: all test run clean
all: hw1.so test

test: $(IR_FILE)

hw1.so: hw1.cpp
	$(CXX) -shared -o $@ $< $(CXXFLAGS) $(LDFLAGS)

$(IR_FILE): $(SOURCE_FILE)
	$(CXX) $(IRFLAGS) -o $@ $<

run: $(IR_FILE) hw1.so
	$(OPT) -disable-output -load-pass-plugin=./hw1.so -passes=hw1 $<

clean:
	rm -f *.o *.ll *.so

```

### Grading Process

- How TA will execute your code:
    1. Build [hw1.so](http://hw1.so/)
    
    ```bash
    $ export LLVM_CONFIG=/path/to/TA/llvm-config
    $ make hw1.so
    
    ```
    
    1. Produce the analysis result
    
    ```bash
    $ export SOURCE_FILE=testcase.c
    $ make test
    $ opt -disable-output -load-pass-plugin=./hw1.so -passes=hw1 <testcase.ll>
    # This should produce testcase.json
    
    ```
    
    1. Check the result against the correct answer:
    
    ```bash
    $ python hw1_checker.py answer.json testcase.json
    
    ```
    
- You can customize your Makefile as long as it conforms to this process.
    - For example, you can modify how the input IR is produced in the Makefile:
    
    ```makefile
    $(IR_FILE): $(SOURCE_FILE)
        $(CXX) $(IRFLAGS) -o $@ $<
        $(OPT) -S -passes=mem2reg,loop-rotate,loop-simplify $@ -o $@
    
    ```
    
    - This allows you to utilize LoopAnalysis in your pass.
- If you encounter any problem, please contact TA([hmlai@pllab.cs.nthu.edu.tw](mailto:hmlai@pllab.cs.nthu.edu.tw)) or post your question on the eeclass discussion.

## Homework 1 - Requirements

- LLVM version:
    - [17.0.2](https://github.com/llvm/llvm-project/releases/tag/llvmorg-17.0.2)
    - ~~You are required to implement the data dependence analysis algorithm on your own and should not use existing LLVM passes.~~
- Deadline:
    - 2025/11/16 23:59
- Please upload the following files to NTHU eeclass:
    - Source Code
        - hw1.cpp (pass source code)
        - Makefile
    - Report
        - hw1_<student_id>_report.pdf
        - up to 4 pages
        - content:
            - Whether you find dependency by solving diophantine equation.
            - Bonus (see Homework 1 - Bonus)
- Late submission points deduction:
    - 10 points for every week late

## Homework 1 - Grading Policy

### Breakdown of Scores: (100% + 10% Bonus)

- test1: 30%
- test2: 30%
- Hidden Test Cases: 30%
- Find depndency by solving diophantine equation - [sample equation solver](https://drive.google.com/file/d/1R9UMQL_K4Qj_YAlq1e9BtA4VxLtDUydr/view?usp=sharing) (must be mentioned in your report): 10%
- Bonus: 10%
- For each test case, 20% of the test case score will be deducted for each wrong part.
    - Example: If you have 3 incorrect dependences for test1 (both missing and extra dependences), you will receive 40%×30% = 12% for test1.

```bash
Flow dependence:
        Correct!

Anti dependence:
        Missing:
                i=9,i=4
                A:S8 --A--> S7

        Extra:
                Entry:  {'src_idx': 15, 'src_statement': 1, 'array': 'BB'}
                        Missing keys:  {'dst_idx', 'src_stmt', 'dst_stmt'}
                        Extra keys:  {'src_statement'}

Output dependence:
        Extra:
                i=8,i=7
                C:S6 --O--> S3

Score:  40

```

### Bonus Policy:

- If the total score exceeds 100%, the additional points from the bonus will be added to your overall course score.

## Homework 1 - Bonus (10%)

### Mixin Pattern (5%)

We've seen in the sample pass that an LLVM pass class is derived from a template instantiation, using itself as a template argument. This is known as the Curiously Recurring Template Pattern (CRTP). Additionally, we can notice that the template name suggests this is a mixin pattern:

```
struct HW1Pass : public PassInfoMixin<HW1Pass>

```

- Please survey and explain why LLVM adopted this pattern for writing pass classes in your report.

### Utilize ADT (5%)

LLVM provides a set of data structures/STL that are optimized for specific scenarios. These purpose-built data types are designed to enhance the efficiency and performance of LLVM-based applications.

For more information on ADT, please refer to [Picking the Right Data Structure for a Task](https://llvm.org/docs/ProgrammersManual.html#picking-the-right-data-structure-for-a-task).

- Try to utilize ADT in your project and write down the experience in your report.

## Reference

[Padua, David A., and Michael J. Wolfe. “Advanced compiler optimizations for supercomputers.” Communications of the ACM 29.12 (1986): 1184-1201.](https://dl.acm.org/doi/abs/10.1145/7902.7904)

## Appendix

### hw1_checker.py

- usage:

```bash
python hw1_checker.py answer.json your_output.json

```

```python
import json
import sys

def load_json(file_path):
    with open(file_path, 'r') as file:
        return json.load(file)

def print_wrong(dependence, wrong_set):
    correct_keys = {"src_idx", "dst_idx", "array", "src_stmt", "dst_stmt"}
    dependence_arrow = {"FlowDependence" : "----->", "AntiDependence" : "--A-->", "OutputDependence" : "--O-->"}
    for wrong_frozen_set in wrong_set:
        wrong_dict = dict(wrong_frozen_set)
        keys = wrong_dict.keys()
        if keys != correct_keys:
            print("\t\tEntry: ", wrong_dict)
            if correct_keys - keys:
                print("\t\t\tMissing keys: ", correct_keys - keys)
            if keys - correct_keys:
                print("\t\t\tExtra keys: ", keys - correct_keys)
            print("")
            return
        print("\t\ti={},i={}".format(wrong_dict["src_idx"], wrong_dict["dst_idx"]))
        print("\t\t{}:S{} {} S{}\n".format(wrong_dict["array"], wrong_dict["src_stmt"], dependence_arrow[dependence], wrong_dict["dst_stmt"]))

def count_wrong(dependence, answer_list, output_list):
    answer_set = {frozenset(sorted(d.items())) for d in answer_list}
    output_set = {frozenset(sorted(d.items())) for d in output_list}
    missing_set = answer_set - output_set
    extra_set = output_set - answer_set
    if missing_set:
        print("\tMissing:")
        print_wrong(dependence, missing_set)
    if extra_set:
        print("\tExtra:")
        print_wrong(dependence, extra_set)
    if not missing_set and not extra_set:
        print("\tCorrect!\n")
    return len(missing_set) + len(extra_set)

def num_wrong_dep(answer_json, output_json):
    answer_flow_list = answer_json["FlowDependence"]
    answer_anti_list = answer_json["AntiDependence"]
    answer_output_list = answer_json["OutputDependence"]

    output_flow_list = output_json["FlowDependence"]
    output_anti_list = output_json["AntiDependence"]
    output_output_list = output_json["OutputDependence"]

    print("Flow dependence:")
    num = count_wrong("FlowDependence", answer_flow_list, output_flow_list)
    print("Anti dependence:")
    num += count_wrong("AntiDependence", answer_anti_list, output_anti_list)
    print("Output dependence:")
    num += count_wrong("OutputDependence", answer_output_list, output_output_list)
    print("Score: ", max(100 - num * 20, 0))

def main(answer_file, output_file):
    answer_json = load_json(answer_file)
    output_json = load_json(output_file)

    num_wrong_dep(answer_json, output_json)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 script.py answer.json output.json")
        sys.exit(1)

    answer_file = sys.argv[1]
    output_file = sys.argv[2]
    main(answer_file, output_file)

```
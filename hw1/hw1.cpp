#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/SmallVector.h"
#include <map>
#include <vector>
#include <set>
#include <string>
#include <climits> // For LLONG_MAX, LLONG_MIN

using namespace llvm;

namespace {

// 最大公因數 (GCD)
long long gcd(long long a, long long b) {
    while (b) {
        a %= b;
        std::swap(a, b);
    }
    return a;
}

// Extended 歐幾里得演算法: 求解 a*x + b*y = gcd(a, b)
void extendedEuclidean(long long a, long long b, long long &x, long long &y) {
    long long x1 = 0, y1 = 1, x0 = 1, y0 = 0;
    while (b) {
        long long q = a / b;
        long long r = a % b;
        a = b;
        b = r;
        
        long long tmp = x1;
        x1 = x0 - q * x1;
        x0 = tmp;
        
        tmp = y1;
        y1 = y0 - q * y1;
        y0 = tmp;
    }
    x = x0;
    y = y0;
}

// 求解 t 的邊界: L <= C + t*S < U
void solveBounds(long long L, long long U, long long C, long long S, 
                 long long &t_min, long long &t_max) {
    if (S == 0) {
        if (L <= C && C < U) return;  // 條件成立，t 範圍不變
        t_min = 1; t_max = 0;         // 條件不成立，無解
        return;
    }
    
    // t*S >= L - C
    long long R_min = L - C;
    // t*S < U - C  =>  t*S <= U - C - 1
    long long R_max_inclusive = U - C - 1;

    long long t_min_new, t_max_new;

    if (S > 0) {
        // t >= ceil(R_min / S)
        t_min_new = (R_min > 0 && R_min % S != 0) ? (R_min / S) + 1 : (R_min / S);
        // t <= floor(R_max_inclusive / S)
        t_max_new = (R_max_inclusive < 0 && R_max_inclusive % S != 0) ? 
                    (R_max_inclusive / S) - 1 : (R_max_inclusive / S);
    } else { // S < 0
        // t*S >= R_min => t <= floor(R_min / S)
        t_max_new = (R_min > 0 && R_min % S != 0) ? (R_min / S) - 1 : (R_min / S);
        // t*S <= R_max_inclusive => t >= ceil(R_max_inclusive / S)
        t_min_new = (R_max_inclusive < 0 && R_max_inclusive % S != 0) ? 
                    (R_max_inclusive / S) + 1 : (R_max_inclusive / S);
    }
    
    t_min = std::max(t_min, t_min_new);
    t_max = std::min(t_max, t_max_new);
}

// 陣列存取資訊
struct ArrayAccess {
    std::string arrayName;  // 陣列名稱
    int stmtNum;           // 語句編號
    bool isStore;          // true: Store, false: Load
    Value *basePtr;        // 基底指標
    int coefficient;       // 索引係數 (c in c*i+d)
    int constant;          // 索引常數 (d in c*i+d)
    Instruction *inst;     // 對應的 LLVM 指令
    
    ArrayAccess() : stmtNum(0), isStore(false), basePtr(nullptr), 
                    coefficient(1), constant(0), inst(nullptr) {}
};

// Dependency 記錄
struct Dependence {
    std::string array;
    int src_stmt;
    int src_idx;
    int dst_stmt;
    int dst_idx;
    
    bool operator<(const Dependence &other) const {
        if (array != other.array) return array < other.array;
        if (src_stmt != other.src_stmt) return src_stmt < other.src_stmt;
        if (src_idx != other.src_idx) return src_idx < other.src_idx;
        if (dst_stmt != other.dst_stmt) return dst_stmt < other.dst_stmt;
        return dst_idx < other.dst_idx;
    }
};

// HW1 Bonus: Mixin Pattern
class HW1Pass : public PassInfoMixin<HW1Pass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
    
private:
    llvm::SmallVector<ArrayAccess> arrayAccesses;  // 使用 LLVM ADT
    std::map<Value*, std::string> arrayNameMap;
    int stmtCounter = 0;
    
    int loopStart = 0;
    int loopEnd = 0;
    
    std::set<Dependence> flowDeps;    // Flow Dependence
    std::set<Dependence> antiDeps;    // Anti Dependence
    std::set<Dependence> outputDeps;  // Output Dependence
    
    void analyzeLoop(Loop *L);
    bool extractIndexExpression(Value *idx, Value *inductionVar, 
                                int &coeff, int &constant);
    void analyzeInstructionSequence(BasicBlock *BB, Value *inductionVar);
    void computeDependences();
    void outputJSON(const std::string &filename);
    std::string getArrayName(Value *ptr);
};

std::string HW1Pass::getArrayName(Value *ptr) {
    // 直接查表
    if (arrayNameMap.find(ptr) != arrayNameMap.end()) {
        return arrayNameMap[ptr];
    }
    
    // 從 GetElementPtr 提取
    if (auto *gep = dyn_cast<GetElementPtrInst>(ptr)) {
        Value *base = gep->getPointerOperand();
        return getArrayName(base);
    }
    
    // 檢查是否有名字
    if (ptr->hasName()) {
        return ptr->getName().str();
    }
    
    return "unknown";
}

bool HW1Pass::extractIndexExpression(Value *idx, Value *inductionVar, 
                                      int &coeff, int &constant) {
    coeff = 0;
    constant = 0;
    
    // Case 1: idx 就是 induction variable (i)
    if (idx == inductionVar) {
        coeff = 1;
        constant = 0;
        return true;
    }
    
    // Case 2: idx 是常數
    if (auto *CI = dyn_cast<ConstantInt>(idx)) {
        coeff = 0;
        constant = CI->getSExtValue();
        return true;
    }

    // 處理 type cast
    if (auto *cast = dyn_cast<CastInst>(idx)) {
        return extractIndexExpression(cast->getOperand(0), inductionVar, coeff, constant);
    }

    // Case 3: idx 是 binary 運算
    if (auto *binOp = dyn_cast<BinaryOperator>(idx)) {
        unsigned opcode = binOp->getOpcode();
        Value *op0 = binOp->getOperand(0);
        Value *op1 = binOp->getOperand(1);
        
        if (opcode == Instruction::Add) {
            int coeff0, const0, coeff1, const1;
            bool ok0 = extractIndexExpression(op0, inductionVar, coeff0, const0);
            bool ok1 = extractIndexExpression(op1, inductionVar, coeff1, const1);
            if (ok0 && ok1) {
                coeff = coeff0 + coeff1;
                constant = const0 + const1;
                return true;
            }
        } else if (opcode == Instruction::Sub) {
            int coeff0, const0, coeff1, const1;
            bool ok0 = extractIndexExpression(op0, inductionVar, coeff0, const0);
            bool ok1 = extractIndexExpression(op1, inductionVar, coeff1, const1);
            if (ok0 && ok1) {
                coeff = coeff0 - coeff1;
                constant = const0 - const1;
                return true;
            }
        } else if (opcode == Instruction::Mul) {
            // c * i or i * c
            if (op0 == inductionVar) {
                if (auto *CI = dyn_cast<ConstantInt>(op1)) {
                    coeff = CI->getSExtValue();
                    constant = 0;
                    return true;
                }
            } else if (op1 == inductionVar) {
                if (auto *CI = dyn_cast<ConstantInt>(op0)) {
                    coeff = CI->getSExtValue();
                    constant = 0;
                    return true;
                }
            }
        }
    }
    
    return false;
}

void HW1Pass::analyzeInstructionSequence(BasicBlock *BB, Value *inductionVar) {
    for (auto &I : *BB) {
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
            Value *ptr = SI->getPointerOperand();
            GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
            
            if (gep) {
                ArrayAccess access;
                access.isStore = true;
                access.inst = SI;
                access.basePtr = gep->getPointerOperand();
                access.arrayName = getArrayName(access.basePtr);
                
                // 取最後一個索引
                Value *idxVal = nullptr;
                for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
                    idxVal = idx->get();
                }
                
                if (idxVal && extractIndexExpression(idxVal, inductionVar, 
                                                    access.coefficient, access.constant)) {
                    access.stmtNum = ++stmtCounter;  // Store 遞增語句編號
                    arrayAccesses.push_back(access);
                    
                    errs() << "  Store to " << access.arrayName 
                           << "[" << access.coefficient << "*i + " << access.constant 
                           << "] (S" << access.stmtNum << ")\n";
                }
            }
            
        } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
            Value *ptr = LI->getPointerOperand();
            GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
            
            if (gep) {
                ArrayAccess access;
                access.isStore = false;
                access.inst = LI;
                access.basePtr = gep->getPointerOperand();
                access.arrayName = getArrayName(access.basePtr);
                
                Value *idxVal = nullptr;
                for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
                    idxVal = idx->get();
                }
                
                if (idxVal && extractIndexExpression(idxVal, inductionVar, 
                                                    access.coefficient, access.constant)) {
                    access.stmtNum = stmtCounter + 1;  // Load 使用當前語句編號
                    arrayAccesses.push_back(access);
                    
                    errs() << "  Load from " << access.arrayName 
                           << "[" << access.coefficient << "*i + " << access.constant 
                           << "] (S" << access.stmtNum << ")\n";
                }
            }
        }
    }
}

void HW1Pass::analyzeLoop(Loop *L) {
    errs() << "Analyzing loop...\n";
    
    BasicBlock *header = L->getHeader();
    BasicBlock *latch = L->getLoopLatch();
    
    // 找 induction variable (PHI node in header)
    Value *inductionVar = nullptr;
    
    for (auto &I : *header) {
        if (auto *phi = dyn_cast<PHINode>(&I)) {
            inductionVar = phi;
            
            // 取初始值
            for (unsigned i = 0; i < phi->getNumIncomingValues(); i++) {
                BasicBlock *incomingBB = phi->getIncomingBlock(i);
                if (incomingBB != latch) {
                    if (auto *CI = dyn_cast<ConstantInt>(phi->getIncomingValue(i))) {
                        loopStart = CI->getSExtValue();
                        errs() << "  Loop start: " << loopStart << "\n";
                    }
                }
            }
            break;
        }
    }
    
    if (!inductionVar) {
        errs() << "  Warning: Could not find induction variable\n";
        return;
    }
    
    // 從 exit condition 找 loop bound
    SmallVector<BasicBlock*, 4> exitingBlocks;
    L->getExitingBlocks(exitingBlocks);
    
    for (auto *exitingBB : exitingBlocks) {
        for (auto &I : *exitingBB) {
            if (auto *br = dyn_cast<BranchInst>(&I)) {
                if (br->isConditional()) {
                    if (auto *cmp = dyn_cast<ICmpInst>(br->getCondition())) {
                        if (auto *CI = dyn_cast<ConstantInt>(cmp->getOperand(1))) {
                            loopEnd = CI->getSExtValue();
                            errs() << "  Loop end: " << loopEnd << "\n";
                        } else if (auto *CI = dyn_cast<ConstantInt>(cmp->getOperand(0))) {
                            loopEnd = CI->getSExtValue();
                            errs() << "  Loop end: " << loopEnd << "\n";
                        }
                    }
                }
            }
        }
    }
    
    stmtCounter = 0;
    
    // 分析 loop body 中的陣列存取
    errs() << "Array accesses:\n";
    for (auto *BB : L->blocks()) {
        analyzeInstructionSequence(BB, inductionVar);
    }
    
    errs() << "Total accesses found: " << arrayAccesses.size() << "\n";
}

void HW1Pass::computeDependences() {
    errs() << "Computing dependences...\n";
    errs() << "  Loop range: [" << loopStart << ", " << loopEnd << ")\n";
    
    long long L = loopStart;
    long long U = loopEnd;

    // 檢查每對陣列存取 (S_src -> S_dst)
    for (size_t i = 0; i < arrayAccesses.size(); i++) {
        for (size_t j = 0; j < arrayAccesses.size(); j++) {
            
            auto &S_src = arrayAccesses[i];
            auto &S_dst = arrayAccesses[j];
            
            // 1. 決定相依性類型
            std::set<Dependence> *depSet = nullptr;
            if (S_src.isStore && !S_dst.isStore) {
                depSet = &flowDeps;    // Write -> Read
            } else if (!S_src.isStore && S_dst.isStore) {
                depSet = &antiDeps;    // Read -> Write
            } else if (S_src.isStore && S_dst.isStore) {
                depSet = &outputDeps;  // Write -> Write
            } else {
                continue;  // Read -> Read, no dependence
            }
            
            // 2. 必須是同一個陣列
            if (S_src.arrayName != S_dst.arrayName) continue;

            // 3. 建立 Diophantine 方程: c1*i1 - c2*i2 = d2 - d1
            long long c1 = S_src.coefficient, d1 = S_src.constant;
            long long c2 = S_dst.coefficient, d2 = S_dst.constant;
            
            long long a = c1;
            long long b = -c2;
            long long C = d2 - d1;

            // 4. GCD 測試
            long long g = gcd(a, b);
            
            if (g == 0) {
                // 兩個索引都是常數
                if (d1 == d2) {
                    Dependence dep;
                    dep.array = S_src.arrayName;
                    dep.src_stmt = S_src.stmtNum;
                    dep.src_idx = d1;
                    dep.dst_stmt = S_dst.stmtNum;
                    dep.dst_idx = d2;
                    depSet->insert(dep);
                }
                continue;
            }

            if (C % g != 0) {
                continue;  // 無整數解
            }

            // 5. 用 Extended Euclidean 求特解
            long long x_prime, y_prime;
            extendedEuclidean(a, b, x_prime, y_prime);
            
            long long i1_0 = x_prime * (C / g);
            long long i2_0 = y_prime * (C / g);

            // 6. 一般解
            // i1(t) = i1_0 + t * (b/g)
            // i2(t) = i2_0 - t * (a/g)
            long long step_i1 = b / g;
            long long step_i2 = -a / g;

            // 7. 求 t 的有效範圍
            long long t_min = -LLONG_MAX;
            long long t_max = LLONG_MAX;
            
            // 邊界 1: loopStart <= i1(t) < loopEnd
            solveBounds(L, U, i1_0, step_i1, t_min, t_max);
            
            // 邊界 2: loopStart <= i2(t) < loopEnd
            solveBounds(L, U, i2_0, step_i2, t_min, t_max);

            // 邊界 3: 時間順序
            if (S_src.stmtNum < S_dst.stmtNum) {
                // S_src 在前，允許 i1 <= i2
                solveBounds(-LLONG_MAX, (i2_0 - i1_0) + 1, 0, (step_i1 - step_i2), t_min, t_max);
            } else {
                // S_src 在後或同敘述，必須 i1 < i2 (loop-carried)
                solveBounds(-LLONG_MAX, (i2_0 - i1_0), 0, (step_i1 - step_i2), t_min, t_max);
            }
            
            // 8. 產生所有相依性
            if (t_min > t_max) continue;
            
            for (long long t = t_min; t <= t_max; t++) {
                Dependence dep;
                dep.array = S_src.arrayName;
                dep.src_stmt = S_src.stmtNum;
                dep.src_idx = (int)(i1_0 + t * step_i1);
                dep.dst_stmt = S_dst.stmtNum;
                dep.dst_idx = (int)(i2_0 + t * step_i2);
                
                depSet->insert(dep);
            }
        }
    }
    
    errs() << "Found " << flowDeps.size() << " flow dependences\n";
    errs() << "Found " << antiDeps.size() << " anti dependences\n";
    errs() << "Found " << outputDeps.size() << " output dependences\n";
}

void HW1Pass::outputJSON(const std::string &filename) {
    std::error_code EC;
    raw_fd_ostream out(filename, EC, sys::fs::OF_None);
    
    if (EC) {
        errs() << "Error opening file " << filename << ": " << EC.message() << "\n";
        return;
    }
    
    out << "{\n";
    
    // Flow Dependence
    out << "  \"FlowDependence\": [";
    bool first = true;
    for (const auto &dep : flowDeps) {
        if (!first) out << ",";
        first = false;
        out << "\n    {\n";
        out << "      \"array\": \"" << dep.array << "\",\n";
        out << "      \"src_stmt\": " << dep.src_stmt << ",\n";
        out << "      \"src_idx\": " << dep.src_idx << ",\n";
        out << "      \"dst_stmt\": " << dep.dst_stmt << ",\n";
        out << "      \"dst_idx\": " << dep.dst_idx << "\n";
        out << "    }";
    }
    out << "\n  ],\n";
    
    // Anti Dependence
    out << "  \"AntiDependence\": [";
    first = true;
    for (const auto &dep : antiDeps) {
        if (!first) out << ",";
        first = false;
        out << "\n    {\n";
        out << "      \"array\": \"" << dep.array << "\",\n";
        out << "      \"src_stmt\": " << dep.src_stmt << ",\n";
        out << "      \"src_idx\": " << dep.src_idx << ",\n";
        out << "      \"dst_stmt\": " << dep.dst_stmt << ",\n";
        out << "      \"dst_idx\": " << dep.dst_idx << "\n";
        out << "    }";
    }
    out << "\n  ],\n";
    
    // Output Dependence
    out << "  \"OutputDependence\": [";
    first = true;
    for (const auto &dep : outputDeps) {
        if (!first) out << ",";
        first = false;
        out << "\n    {\n";
        out << "      \"array\": \"" << dep.array << "\",\n";
        out << "      \"src_stmt\": " << dep.src_stmt << ",\n";
        out << "      \"src_idx\": " << dep.src_idx << ",\n";
        out << "      \"dst_stmt\": " << dep.dst_stmt << ",\n";
        out << "      \"dst_idx\": " << dep.dst_idx << "\n";
        out << "    }";
    }
    out << "\n  ]\n";
    
    out << "}\n";
    out.close();
    
    errs() << "Output written to " << filename << "\n";
}

PreservedAnalyses HW1Pass::run(Function &F, FunctionAnalysisManager &FAM) {
    errs() << "[HW1]: " << F.getName() << '\n';
    
    // Get loop info.
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    
    // 從 alloca 建立陣列名稱對應表
    errs() << "Building array name map...\n";
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *AI = dyn_cast<AllocaInst>(&I)) {
                if (AI->getAllocatedType()->isArrayTy()) {
                    std::string name = AI->hasName() ? AI->getName().str() : "unnamed";
                    arrayNameMap[AI] = name;
                    errs() << "  Found array: " << name << "\n";
                }
            }
        }
    }
    
    // 處理每個 loop
    int loopCount = 0;
    for (auto *L : LI) {
        loopCount++;
        errs() << "\nProcessing loop #" << loopCount << "\n";
        analyzeLoop(L);
    }
    
    if (loopCount == 0) {
        errs() << "Warning: No loops found in function!\n";
    }
    
    // 計算相依性
    computeDependences();
    
    // 輸出 JSON
    std::string moduleName = F.getParent()->getSourceFileName();
    if (moduleName.empty()) {
        moduleName = F.getParent()->getName().str();
    }
    
    size_t dotPos = moduleName.rfind('.');
    if (dotPos != std::string::npos) {
        moduleName = moduleName.substr(0, dotPos);
    }
    
    size_t slashPos = moduleName.rfind('/');
    if (slashPos != std::string::npos) {
        moduleName = moduleName.substr(slashPos + 1);
    }
    
    std::string outputFile = moduleName + ".json";
    outputJSON(outputFile);
    
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

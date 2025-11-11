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
#include <map>
#include <vector>
#include <set>
#include <string>

using namespace llvm;

namespace {

// Data structure to store array access information
struct ArrayAccess {
    std::string arrayName;
    int stmtNum;
    bool isStore;
    Value *basePtr;
    int coefficient;
    int constant;
    Instruction *inst;
    
    ArrayAccess() : stmtNum(0), isStore(false), basePtr(nullptr), 
                    coefficient(1), constant(0), inst(nullptr) {}
};

// Dependence record
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

class HW1Pass : public PassInfoMixin<HW1Pass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
    
private:
    std::vector<ArrayAccess> arrayAccesses;
    std::map<Value*, std::string> arrayNameMap;
    int stmtCounter = 0;
    
    int loopStart = 0;
    int loopEnd = 0;
    
    std::set<Dependence> flowDeps;
    std::set<Dependence> antiDeps;
    std::set<Dependence> outputDeps;
    
    void analyzeLoop(Loop *L);
    bool extractIndexExpression(Value *idx, Value *inductionVar, 
                                int &coeff, int &constant);
    void analyzeInstructionSequence(BasicBlock *BB, Value *inductionVar);
    void computeDependences();
    void outputJSON(const std::string &filename);
    std::string getArrayName(Value *ptr);
};

std::string HW1Pass::getArrayName(Value *ptr) {
    // Try direct lookup
    if (arrayNameMap.find(ptr) != arrayNameMap.end()) {
        return arrayNameMap[ptr];
    }
    
    // Try to extract from GetElementPtr
    if (auto *gep = dyn_cast<GetElementPtrInst>(ptr)) {
        Value *base = gep->getPointerOperand();
        return getArrayName(base);
    }
    
    // Check if it has a name
    if (ptr->hasName()) {
        return ptr->getName().str();
    }
    
    return "unknown";
}

bool HW1Pass::extractIndexExpression(Value *idx, Value *inductionVar, 
                                      int &coeff, int &constant) {
    coeff = 0;
    constant = 0;
    
    // Case 1: idx is the induction variable itself (i)
    if (idx == inductionVar) {
        coeff = 1;
        constant = 0;
        return true;
    }
    
    // Case 2: idx is a constant
    if (auto *CI = dyn_cast<ConstantInt>(idx)) {
        coeff = 0;
        constant = CI->getSExtValue();
        return true;
    }

    if (auto *cast = dyn_cast<CastInst>(idx)) {
        return extractIndexExpression(cast->getOperand(0), inductionVar, coeff, constant);
    }

    // Case 3: idx is a binary operation
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
    
    // Case 4: idx might be a Load of the induction variable
    if (auto *LI = dyn_cast<LoadInst>(idx)) {
        // Sometimes the induction variable is loaded before use
        // We need to trace back
        return false; // For now, we don't handle this
    }
    
    return false;
}

void HW1Pass::analyzeInstructionSequence(BasicBlock *BB, Value *inductionVar) {
    // 處理 Load 和 Store 指令
    for (auto &I : *BB) {
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
            Value *ptr = SI->getPointerOperand();
            
            // ptr 是一個 Value，需要找到定義它的 GEP 指令
            GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
            
            if (gep) {
                ArrayAccess access;
                access.isStore = true;
                access.inst = SI;
                access.basePtr = gep->getPointerOperand();
                access.arrayName = getArrayName(access.basePtr);
                
                // 獲取最後一個索引
                Value *idxVal = nullptr;
                for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
                    idxVal = idx->get();
                }
                
                if (idxVal && extractIndexExpression(idxVal, inductionVar, 
                                                    access.coefficient, access.constant)) {
                    // Store 遞增語句編號
                    access.stmtNum = ++stmtCounter;
                    arrayAccesses.push_back(access);
                    
                    errs() << "  Store to " << access.arrayName 
                           << "[" << access.coefficient << "*i + " << access.constant 
                           << "] (S" << access.stmtNum << ")\n";
                }
            }
            
        } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
            Value *ptr = LI->getPointerOperand();
            
            // 同樣地，找到產生這個指標的 GEP 指令
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
                    // Load 使用當前的語句編號（Store 尚未遞增）
                    access.stmtNum = stmtCounter + 1;
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
    
    // Get loop header
    BasicBlock *header = L->getHeader();
    BasicBlock *latch = L->getLoopLatch();
    
    // Find induction variable (PHINode in header)
    Value *inductionVar = nullptr;
    
    for (auto &I : *header) {
        if (auto *phi = dyn_cast<PHINode>(&I)) {
            inductionVar = phi;
            
            // Extract initial value
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
    
    // Find loop bound from exit condition
    SmallVector<BasicBlock*, 4> exitingBlocks;
    L->getExitingBlocks(exitingBlocks);
    
    for (auto *exitingBB : exitingBlocks) {
        for (auto &I : *exitingBB) {
            if (auto *br = dyn_cast<BranchInst>(&I)) {
                if (br->isConditional()) {
                    if (auto *cmp = dyn_cast<ICmpInst>(br->getCondition())) {
                        // Check if comparing with induction variable
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
    
    // Reset statement counter
    stmtCounter = 0;
    
    // Analyze array accesses in loop body
    errs() << "Array accesses:\n";
    for (auto *BB : L->blocks()) {
        // if (BB == header) continue; // Skip header
        analyzeInstructionSequence(BB, inductionVar);
    }
    
    errs() << "Total accesses found: " << arrayAccesses.size() << "\n";
}

void HW1Pass::computeDependences() {
    errs() << "Computing dependences...\n";
    errs() << "  Loop range: [" << loopStart << ", " << loopEnd << ")\n";
    
    // For each pair of array accesses
    for (size_t i = 0; i < arrayAccesses.size(); i++) {
        for (size_t j = 0; j < arrayAccesses.size(); j++) {
            auto &acc1 = arrayAccesses[i];
            auto &acc2 = arrayAccesses[j];
            
            // Must be same array
            if (acc1.arrayName != acc2.arrayName) continue;
            
            // Must be different statements
            if (acc1.stmtNum == acc2.stmtNum) continue;
            
            // Check all iteration combinations
            int c1 = acc1.coefficient, d1 = acc1.constant;
            int c2 = acc2.coefficient, d2 = acc2.constant;
            
            for (int i1 = loopStart; i1 < loopEnd; i1++) {
                for (int i2 = loopStart; i2 < loopEnd; i2++) {
                    int idx1 = c1 * i1 + d1;
                    int idx2 = c2 * i2 + d2;
                    
                    if (idx1 != idx2) continue; // 必須存取相同的元素

                    //--- 開始修正 ---
                    bool dependence_exists = false;

                    if (i1 < i2) {
                        // Case 1: Loop-carried dependence (跨迭代)
                        // i1 必定在 i2 之前執行
                        dependence_exists = true;
                    } else if (i1 == i2) {
                        // Case 2: Loop-independent dependence (同迭代)
                        // 僅當 acc1 的敘句編號小於 acc2 時，才存在相依性
                        if (acc1.stmtNum < acc2.stmtNum) {
                            dependence_exists = true;
                        }
                    }
                    // else (i1 > i2) 的情況會被 acc1 和 acc2 角色互換時的 (i1 < i2) 捕捉到
                    
                    if (dependence_exists) {
                        Dependence dep;
                        dep.array = acc1.arrayName;
                        dep.src_stmt = acc1.stmtNum;
                        dep.src_idx = i1;
                        dep.dst_stmt = acc2.stmtNum;
                        dep.dst_idx = i2;
                        
                        // 根據存取類型判斷相依性
                        if (acc1.isStore && !acc2.isStore) {
                            // Write (acc1) -> Read (acc2): Flow
                            flowDeps.insert(dep);
                        } else if (!acc1.isStore && acc2.isStore) {
                            // Read (acc1) -> Write (acc2): Anti
                            antiDeps.insert(dep);
                        } else if (acc1.isStore && acc2.isStore) {
                            // Write (acc1) -> Write (acc2): Output
                            outputDeps.insert(dep);
                        }
                    }                 

                }
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
    
    // Get loop info
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    
    // Build array name map from allocas
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
    
    // Process each loop
    int loopCount = 0;
    for (auto *L : LI) {
        loopCount++;
        errs() << "\nProcessing loop #" << loopCount << "\n";
        analyzeLoop(L);
    }
    
    if (loopCount == 0) {
        errs() << "Warning: No loops found in function!\n";
    }
    
    // Compute dependences
    computeDependences();
    
    // Output to JSON file
    std::string moduleName = F.getParent()->getSourceFileName();
    if (moduleName.empty()) {
        moduleName = F.getParent()->getName().str();
    }
    
    size_t dotPos = moduleName.rfind('.');
    if (dotPos != std::string::npos) {
        moduleName = moduleName.substr(0, dotPos);
    }
    
    // Remove path if present
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
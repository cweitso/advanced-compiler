#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include <map>
#include <set>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <sstream>

using namespace llvm;

namespace {

// 表示一個變數樹（如 x, *p, **pp）
struct VarTree {
    std::string name;           // 基礎變數名
    int derefLevel;             // 解引用層級：0=x, 1=*p, 2=**pp
    
    VarTree(const std::string& n = "", int level = 0) 
        : name(n), derefLevel(level) {}
    
    // 獲取完整表示（如 "*p", "**pp"）
    std::string toString() const {
        std::string prefix(derefLevel, '*');
        return prefix + name;
    }
    
    bool operator<(const VarTree& other) const {
        if (name != other.name) return name < other.name;
        return derefLevel < other.derefLevel;
    }
    
    bool operator==(const VarTree& other) const {
        return name == other.name && derefLevel == other.derefLevel;
    }
};

// 數據依賴
struct Dependence {
    std::string var;
    int srcStmt;
    int dstStmt;
    std::string type;  // "flow" 或 "output"
    
    bool operator<(const Dependence& other) const {
        if (var != other.var) return var < other.var;
        if (srcStmt != other.srcStmt) return srcStmt < other.srcStmt;
        if (dstStmt != other.dstStmt) return dstStmt < other.dstStmt;
        return type < other.type;
    }
};

// 等價對
struct EquivPair {
    std::string first;   // 如 "*p"
    std::string second;  // 如 "x"
    
    bool operator<(const EquivPair& other) const {
        if (first != other.first) return first < other.first;
        return second < other.second;
    }
};

// 語句分析結果
struct StmtAnalysis {
    int stmtNum;
    std::set<std::string> TREF;      // 引用的變數
    std::set<std::string> TGEN;      // 生成的變數
    std::set<Dependence> DEP;        // 數據依賴
    std::map<std::string, int> TDEF; // 到達定義
    std::set<EquivPair> TEQUIV;      // 等價關係
};

class PointerAnalyzer {
private:
    // 指向關係：pointer -> pointee（如 p -> x 表示 p 指向 x）
    std::map<std::string, std::string> pointsTo;
    
    // 當前 TDEF
    std::map<std::string, int> currentTDEF;
    
    // 當前 TEQUIV
    std::set<EquivPair> currentTEQUIV;
    
    // 所有語句的分析結果
    std::vector<StmtAnalysis> results;
    
    // LLVM Value 到變數名的映射
    std::map<Value*, std::string> valueNames;
    
    // 語句計數器
    int stmtCounter = 0;

public:
    // 獲取變數名稱
    std::string getVarName(Value* V) {
        if (!V) return "";
        
        // 如果已經有映射，直接返回
        if (valueNames.count(V)) {
            return valueNames[V];
        }
        
        // 嘗試從 LLVM 獲取名稱
        if (V->hasName()) {
            std::string name = V->getName().str();
            valueNames[V] = name;
            return name;
        }
        
        // 對於 alloca 指令，嘗試獲取調試信息
        if (auto* AI = dyn_cast<AllocaInst>(V)) {
            // 檢查是否有調試信息
            for (auto* U : AI->users()) {
                if (auto* DVI = dyn_cast<DbgVariableIntrinsic>(U)) {
                    if (auto* DIVar = DVI->getVariable()) {
                        std::string name = DIVar->getName().str();
                        valueNames[V] = name;
                        return name;
                    }
                }
            }
        }
        
        return "";
    }
    
    // 分析 store 指令
    // store 指令：store <value>, <pointer>
    // 表示將 value 存入 pointer 指向的位置
    void analyzeStore(StoreInst* SI, int stmtNum) {
        StmtAnalysis analysis;
        analysis.stmtNum = stmtNum;
        
        Value* valueOp = SI->getValueOperand();   // 被存儲的值
        Value* ptrOp = SI->getPointerOperand();   // 存儲目標地址
        
        // 分析寫入目標（TGEN）
        std::set<std::string> genSet;
        std::string targetVar = analyzeWriteTarget(ptrOp, genSet, analysis.TREF);
        
        // 分析讀取的變數（TREF）
        analyzeReadValue(valueOp, analysis.TREF);
        
        analysis.TGEN = genSet;
        
        // 計算依賴
        computeDependences(analysis);
        
        // 更新 TDEF
        for (const auto& var : analysis.TGEN) {
            currentTDEF[var] = stmtNum;
        }
        analysis.TDEF = currentTDEF;
        
        // 更新等價關係（如果是指針賦值）
        updateEquivalences(SI, stmtNum);
        analysis.TEQUIV = currentTEQUIV;
        
        results.push_back(analysis);
    }
    
    // 分析寫入目標，返回基礎變數名，並填充 genSet
    std::string analyzeWriteTarget(Value* ptr, std::set<std::string>& genSet, 
                                   std::set<std::string>& refSet) {
        // 情況 1：直接是 alloca（如 store ... %x）
        if (auto* AI = dyn_cast<AllocaInst>(ptr)) {
            std::string name = getVarName(AI);
            if (!name.empty()) {
                genSet.insert(name);
                return name;
            }
        }
        
        // 情況 2：是 load 的結果（如 store ... %1，其中 %1 = load %p）
        // 這表示間接寫入：*p = ...
        if (auto* LI = dyn_cast<LoadInst>(ptr)) {
            Value* loadPtr = LI->getPointerOperand();
            
            // 一級間接：*p = ...
            if (auto* AI = dyn_cast<AllocaInst>(loadPtr)) {
                std::string ptrName = getVarName(AI);
                if (!ptrName.empty()) {
                    // 需要讀取 p 才能知道寫到哪
                    refSet.insert(ptrName);
                    
                    // 如果 p 等價於某個表達式（如 *pp），也加入 refSet
                    for (const auto& eq : currentTEQUIV) {
                        if (eq.second == ptrName) {
                            refSet.insert(eq.first);
                        } else if (eq.first == ptrName) {
                            refSet.insert(eq.second);
                        }
                    }
                    
                    // TGEN 包含 *p
                    std::string derefName = "*" + ptrName;
                    genSet.insert(derefName);
                    
                    // 如果 *p 等價於某個變數，也加入 TGEN
                    addEquivalentVars(derefName, genSet);
                    
                    return derefName;
                }
            }
            
            // 二級間接：**pp = ...（ptr 是 load (load %pp) 的結果）
            if (auto* LI2 = dyn_cast<LoadInst>(loadPtr)) {
                Value* loadPtr2 = LI2->getPointerOperand();
                if (auto* AI = dyn_cast<AllocaInst>(loadPtr2)) {
                    std::string ppName = getVarName(AI);
                    if (!ppName.empty()) {
                        refSet.insert(ppName);
                        refSet.insert("*" + ppName);
                        
                        // 添加 *pp 的等價變數（如 p）到 refSet
                        std::string starPP = "*" + ppName;
                        for (const auto& eq : currentTEQUIV) {
                            if (eq.first == starPP) {
                                refSet.insert(eq.second);
                            } else if (eq.second == starPP) {
                                refSet.insert(eq.first);
                            }
                        }
                        
                        std::string derefName = "**" + ppName;
                        genSet.insert(derefName);
                        addEquivalentVars(derefName, genSet);
                        
                        return derefName;
                    }
                }
            }
        }
        
        return "";
    }
    
    // 分析讀取的值
    void analyzeReadValue(Value* val, std::set<std::string>& refSet) {
        // 如果是常數，不需要處理
        if (isa<Constant>(val)) return;
        
        // 如果是取地址操作（& 運算），不算引用
        // 在 LLVM IR 中，&x 直接就是 alloca 的結果，不會有額外的 load
        
        // 如果是 load 指令，追蹤被讀取的變數
        if (auto* LI = dyn_cast<LoadInst>(val)) {
            analyzeLoad(LI, refSet);
        }
        
        // 如果是二元運算，遞歸分析操作數
        if (auto* BO = dyn_cast<BinaryOperator>(val)) {
            analyzeReadValue(BO->getOperand(0), refSet);
            analyzeReadValue(BO->getOperand(1), refSet);
        }
    }
    
    // 分析 load 指令
    void analyzeLoad(LoadInst* LI, std::set<std::string>& refSet) {
        Value* ptr = LI->getPointerOperand();
        
        // 直接從 alloca 加載：讀取變數 x
        if (auto* AI = dyn_cast<AllocaInst>(ptr)) {
            std::string name = getVarName(AI);
            if (!name.empty()) {
                refSet.insert(name);
                
                // 如果這個變數等價於某個指針表達式，也加入
                // 例如：如果 y 等價於 *p，讀取 y 也意味著讀取 *p
                for (const auto& eq : currentTEQUIV) {
                    if (eq.second == name) {
                        refSet.insert(eq.first);
                    }
                }
            }
            return;
        }
        
        // 間接加載：load (load %p) 表示讀取 *p
        if (auto* LI2 = dyn_cast<LoadInst>(ptr)) {
            Value* ptr2 = LI2->getPointerOperand();
            if (auto* AI = dyn_cast<AllocaInst>(ptr2)) {
                std::string ptrName = getVarName(AI);
                if (!ptrName.empty()) {
                    refSet.insert(ptrName);
                    std::string derefName = "*" + ptrName;
                    refSet.insert(derefName);
                    
                    // 添加 *p 的等價變數（例如 y）
                    for (const auto& eq : currentTEQUIV) {
                        if (eq.first == derefName) {
                            refSet.insert(eq.second);
                        } else if (eq.second == derefName) {
                            refSet.insert(eq.first);
                        }
                    }
                }
            }
            
            // 二級間接加載：load (load (load %pp)) 表示讀取 **pp
            if (auto* LI3 = dyn_cast<LoadInst>(ptr2)) {
                Value* ptr3 = LI3->getPointerOperand();
                if (auto* AI = dyn_cast<AllocaInst>(ptr3)) {
                    std::string ppName = getVarName(AI);
                    if (!ppName.empty()) {
                        refSet.insert(ppName);
                        refSet.insert("*" + ppName);
                        std::string derefName = "**" + ppName;
                        refSet.insert(derefName);
                        
                        // 添加 **pp 的等價變數
                        for (const auto& eq : currentTEQUIV) {
                            if (eq.first == derefName) {
                                refSet.insert(eq.second);
                            } else if (eq.second == derefName) {
                                refSet.insert(eq.first);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 添加等價變數到集合
    void addEquivalentVars(const std::string& var, std::set<std::string>& varSet) {
        for (const auto& eq : currentTEQUIV) {
            if (eq.first == var) {
                varSet.insert(eq.second);
            } else if (eq.second == var) {
                varSet.insert(eq.first);
            }
        }
    }
    
    // 計算數據依賴
    void computeDependences(StmtAnalysis& analysis) {
        // Flow 依賴：TREF 中的變數在 TDEF 中有定義
        for (const auto& var : analysis.TREF) {
            if (currentTDEF.count(var)) {
                Dependence dep;
                dep.var = var;
                dep.srcStmt = currentTDEF[var];
                dep.dstStmt = analysis.stmtNum;
                dep.type = "flow";
                analysis.DEP.insert(dep);
            }
        }
        
        // Output 依賴：TGEN 中的變數在 TDEF 中有定義
        for (const auto& var : analysis.TGEN) {
            if (currentTDEF.count(var)) {
                Dependence dep;
                dep.var = var;
                dep.srcStmt = currentTDEF[var];
                dep.dstStmt = analysis.stmtNum;
                dep.type = "output";
                analysis.DEP.insert(dep);
            }
        }
    }
    
    // 更新等價關係
    void updateEquivalences(StoreInst* SI, int stmtNum) {
        Value* valueOp = SI->getValueOperand();
        Value* ptrOp = SI->getPointerOperand();
        
        // 檢查是否是指針賦值（value 是某個 alloca 的地址）
        // p = &x 在 IR 中是：store i32* %x, i32** %p
        if (auto* AI = dyn_cast<AllocaInst>(valueOp)) {
            // valueOp 是 &x（x 的地址）
            std::string pointee = getVarName(AI);
            
            // 情況1: 直接賦值 p = &x
            // ptrOp 應該是 p 的 alloca
            if (auto* PtrAI = dyn_cast<AllocaInst>(ptrOp)) {
                std::string pointer = getVarName(PtrAI);
                
                if (!pointer.empty() && !pointee.empty()) {
                    // 建立 p -> x 的指向關係
                    pointsTo[pointer] = pointee;
                    
                    // 添加等價關係：*p 等價於 x
                    EquivPair eq;
                    eq.first = "*" + pointer;
                    eq.second = pointee;
                    currentTEQUIV.insert(eq);
                    
                    // 傳遞等價關係
                    propagateEquivalences();
                }
            }
            
            // 情況2: 間接賦值 *pp = &y (ptrOp 是 load %pp 的結果)
            // 這意味著 pp 指向的指針現在指向 y
            if (auto* LI = dyn_cast<LoadInst>(ptrOp)) {
                Value* loadPtr = LI->getPointerOperand();
                if (auto* PtrAI = dyn_cast<AllocaInst>(loadPtr)) {
                    std::string ppName = getVarName(PtrAI);
                    
                    if (!ppName.empty() && !pointee.empty()) {
                        // pp 指向 p，所以 *pp = &y 意味著 p = &y
                        // 找出 pp 指向什麼（也就是 *pp 等價於什麼）
                        std::string targetPointer;
                        for (const auto& eq : currentTEQUIV) {
                            if (eq.first == "*" + ppName) {
                                targetPointer = eq.second;
                                break;
                            }
                        }
                        
                        if (!targetPointer.empty()) {
                            // 更新 targetPointer -> pointee 的指向關係
                            pointsTo[targetPointer] = pointee;
                            
                            // 清除舊的等價關係，重新建立
                            rebuildEquivalences();
                        }
                    }
                }
            }
        }
    }
    
    // 重新建立所有等價關係
    void rebuildEquivalences() {
        currentTEQUIV.clear();
        
        // 根據 pointsTo 建立基本等價關係
        for (const auto& pt : pointsTo) {
            EquivPair eq;
            eq.first = "*" + pt.first;
            eq.second = pt.second;
            currentTEQUIV.insert(eq);
        }
        
        // 傳遞等價關係
        propagateEquivalences();
    }
    
    // 傳遞等價關係
    // 如果 pp -> p 且 p -> x，則 **pp 等價於 x
    void propagateEquivalences() {
        bool changed = true;
        while (changed) {
            changed = false;
            
            for (const auto& pt1 : pointsTo) {
                // pt1: pointer1 -> pointee1
                // 如果 pointee1 也是一個指針，且指向 pointee2
                if (pointsTo.count(pt1.second)) {
                    std::string pointer1 = pt1.first;
                    std::string pointer2 = pt1.second;
                    std::string pointee2 = pointsTo[pointer2];
                    
                    // *pp 等價於 p
                    EquivPair eq1;
                    eq1.first = "*" + pointer1;
                    eq1.second = pointer2;
                    if (currentTEQUIV.find(eq1) == currentTEQUIV.end()) {
                        currentTEQUIV.insert(eq1);
                        changed = true;
                    }
                    
                    // **pp 等價於 *p（也就是等價於 pointee2）
                    EquivPair eq2;
                    eq2.first = "**" + pointer1;
                    eq2.second = pointee2;
                    if (currentTEQUIV.find(eq2) == currentTEQUIV.end()) {
                        currentTEQUIV.insert(eq2);
                        changed = true;
                    }
                }
            }
        }
    }
    
    // 輸出 JSON 格式結果
    void outputJSON(const std::string& filename) {
        std::ofstream out(filename);
        out << "{\n";
        
        for (size_t i = 0; i < results.size(); i++) {
            const auto& r = results[i];
            out << "  \"S" << r.stmtNum << "\": {\n";
            
            // TREF
            out << "    \"TREF\": [";
            bool first = true;
            for (const auto& v : r.TREF) {
                if (!first) out << ", ";
                out << "\"" << v << "\"";
                first = false;
            }
            out << "],\n";
            
            // TGEN
            out << "    \"TGEN\": [";
            first = true;
            for (const auto& v : r.TGEN) {
                if (!first) out << ", ";
                out << "\"" << v << "\"";
                first = false;
            }
            out << "],\n";
            
            // DEP
            out << "    \"DEP\": [";
            first = true;
            for (const auto& d : r.DEP) {
                if (!first) out << ",";
                out << "\n      {";
                out << "\"var\": \"" << d.var << "\", ";
                out << "\"src_stmt\": " << d.srcStmt << ", ";
                out << "\"dst_stmt\": " << d.dstStmt << ", ";
                out << "\"type\": \"" << d.type << "\"";
                out << "}";
                first = false;
            }
            if (!r.DEP.empty()) out << "\n    ";
            out << "],\n";
            
            // TDEF
            out << "    \"TDEF\": {";
            first = true;
            for (const auto& td : r.TDEF) {
                if (!first) out << ", ";
                out << "\"" << td.first << "\": " << td.second;
                first = false;
            }
            out << "},\n";
            
            // TEQUIV
            out << "    \"TEQUIV\": [";
            first = true;
            for (const auto& eq : r.TEQUIV) {
                if (!first) out << ", ";
                out << "[\"" << eq.first << "\", \"" << eq.second << "\"]";
                first = false;
            }
            out << "]\n";
            
            out << "  }";
            if (i < results.size() - 1) out << ",";
            out << "\n";
        }
        
        out << "}\n";
        out.close();
    }
    
    // 分析整個函數
    void analyzeFunction(Function& F) {
        stmtCounter = 0;
        currentTDEF.clear();
        currentTEQUIV.clear();
        pointsTo.clear();
        results.clear();
        valueNames.clear();
        
        // 第一遍：收集所有變數名
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* AI = dyn_cast<AllocaInst>(&I)) {
                    getVarName(AI);
                }
            }
        }
        
        // 第二遍：分析每個 store 指令
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* SI = dyn_cast<StoreInst>(&I)) {
                    stmtCounter++;
                    analyzeStore(SI, stmtCounter);
                }
            }
        }
        
        // 輸出結果
        std::string funcName = F.getName().str();
        outputJSON(funcName + ".json");
    }
};

struct HW2Pass : public PassInfoMixin<HW2Pass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        // 跳過聲明（沒有函數體的函數）
        if (F.isDeclaration()) {
            return PreservedAnalyses::all();
        }
        
        errs() << "[HW2]: Analyzing function " << F.getName() << "\n";
        
        PointerAnalyzer analyzer;
        analyzer.analyzeFunction(F);
        
        return PreservedAnalyses::all();
    }
};

} // end anonymous namespace

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "HW2Pass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "hw2") {
                            FPM.addPass(HW2Pass());
                            return true;
                        }
                        return false;
                    });
            }};
}
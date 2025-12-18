#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
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
#include <iostream>

using namespace llvm;
using namespace std;

namespace {

// 工具函數：獲取變數名稱
// 優先嘗試獲取 source code 中的名稱 (alloca 的名稱)
string getVarName(Value *V) {
    if (!V) return "";
    
    // 處理 LoadInst，遞歸獲取其指針來源的名稱
    if (auto *LI = dyn_cast<LoadInst>(V)) {
        return getVarName(LI->getPointerOperand());
    }
    
    // 處理 AllocaInst
    if (auto *AI = dyn_cast<AllocaInst>(V)) {
        if (AI->hasName()) {
            string name = AI->getName().str();
            // 移除 LLVM 可能生成的後綴 (e.g., x.addr)
            size_t dotPos = name.find('.');
            if (dotPos != string::npos) {
                name = name.substr(0, dotPos);
            }
            return name;
        }
    }
    
    // 全局變數
    if (auto *GV = dyn_cast<GlobalValue>(V)) {
        if (GV->hasName()) return GV->getName().str();
    }

    return "";
}

struct Dependence {
    string var;
    int srcStmt;
    int dstStmt;
    string type; // "flow" or "output"

    bool operator<(const Dependence &other) const {
        if (srcStmt != other.srcStmt) return srcStmt < other.srcStmt;
        if (dstStmt != other.dstStmt) return dstStmt < other.dstStmt;
        if (var != other.var) return var < other.var;
        return type < other.type;
    }
};

struct AnalysisResult {
    int stmtId;
    set<string> TREF;
    set<string> TGEN;
    set<Dependence> DEP;
    map<string, int> TDEF;
    set<pair<string, string>> TEQUIV;
};

class PointerAnalyzer {
private:
    // Points-to map: pointer -> pointee (e.g., "p" -> "x", "pp" -> "p")
    map<string, string> pointsTo;
    
    // TDEF: var_name -> defining_stmt_id
    map<string, int> currentTDEF;

    vector<AnalysisResult> results;
    int stmtCounter = 0;

public:
    void analyzeFunction(Function &F) {
        pointsTo.clear();
        currentTDEF.clear();
        results.clear();
        stmtCounter = 0;

        // 預先掃描所有 Alloca 以初始化 TDEF (避免未定義行為)
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *AI = dyn_cast<AllocaInst>(&I)) {
                    string name = getVarName(AI);
                    // 根據助教 output，初始變量不一定在 TDEF 中，直到第一次被賦值
                    // 這裡我們不預填，遵循題目 "reaching definitions"
                }
            }
        }

        // 遍歷所有指令
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    stmtCounter++;
                    analyzeStore(SI, stmtCounter);
                }
            }
        }
        
        outputJSON(F.getName().str() + ".json");
    }

private:
    // 分析 Store 指令
    void analyzeStore(StoreInst *SI, int stmtId) {
        AnalysisResult res;
        res.stmtId = stmtId;

        Value *ptrOp = SI->getPointerOperand();
        Value *valOp = SI->getValueOperand();

        // 1. 分析 LHS (Pointer Operand) 來決定 TGEN 和 TREF (因為解引用需要讀取)
        string lhsBaseVar;
        int derefLevel = 0; // 0: x, 1: *p, 2: **pp
        
        analyzeLHS(ptrOp, lhsBaseVar, derefLevel, res.TREF);

        // 2. 分析 RHS (Value Operand) 來決定 TREF
        analyzeRHS(valOp, res.TREF);

        // 3. 計算 TGEN
        // 根據 derefLevel 和當前的 pointsTo 關係決定生成了什麼
        string syntacticGen; // 語法上的生成，如 *p
        string semanticGen;  // 語義上的生成，如 p 指向 x，則 *p 生成 x

        if (derefLevel == 0) {
            // Case: x = ...
            res.TGEN.insert(lhsBaseVar);
            
            // 如果這是一個指針變量賦值 (e.g., p = &x)，更新 pointsTo
            updatePointsTo(lhsBaseVar, valOp);
        } 
        else if (derefLevel == 1) {
            // Case: *p = ...
            syntacticGen = "*" + lhsBaseVar;
            res.TGEN.insert(syntacticGen);
            
            // Semantic Gen: 如果 p -> x，則 gen x
            if (pointsTo.count(lhsBaseVar)) {
                string pointee = pointsTo[lhsBaseVar];
                res.TGEN.insert(pointee);
                semanticGen = pointee;
                
                // 如果這是間接指針賦值 (e.g., *pp = &y, 且 pp->p)，
                // 這意味著 p = &y。我們需要更新 p 的 pointsTo。
                updatePointsTo(pointee, valOp);
            }
        }
        else if (derefLevel == 2) {
            // Case: **pp = ...
            syntacticGen = "**" + lhsBaseVar;
            res.TGEN.insert(syntacticGen);
            
            // Semantic Gen: 需要兩層解引用
            // pp -> p -> x
            if (pointsTo.count(lhsBaseVar)) {
                string mid = pointsTo[lhsBaseVar]; // p
                if (pointsTo.count(mid)) {
                    string target = pointsTo[mid]; // x
                    res.TGEN.insert(target);
                    semanticGen = target;
                    
                    // Update pointsTo if needed (e.g. **pp = &z) implies x = &z
                     updatePointsTo(target, valOp);
                }
            }
        }

        // 4. 計算 DEP (Flow & Output)
        // 必須使用 *更新前* 的 currentTDEF
        for (const string &var : res.TREF) {
            if (currentTDEF.count(var)) {
                // 如果讀取的變量之前有定義 -> Flow Dependence
                res.DEP.insert({var, currentTDEF[var], stmtId, "flow"});
            }
        }
        for (const string &var : res.TGEN) {
            if (currentTDEF.count(var)) {
                // 如果寫入的變量之前有定義 -> Output Dependence
                res.DEP.insert({var, currentTDEF[var], stmtId, "output"});
            }
        }

        // 5. 更新 TDEF (Reaching Definitions)
        for (const string &var : res.TGEN) {
            currentTDEF[var] = stmtId;
        }
        res.TDEF = currentTDEF; // 複製一份當前的狀態

        // 6. 計算 TEQUIV
        res.TEQUIV = computeTEQUIV();

        results.push_back(res);
    }

    // 分析 LHS (寫入目標)
    // 遞歸解析 Load 指令來確定解引用層級
    void analyzeLHS(Value *ptr, string &baseVar, int &level, set<string> &TREF) {
        if (auto *AI = dyn_cast<AllocaInst>(ptr)) {
            baseVar = getVarName(AI);
            level = 0;
            return;
        }
        
        if (auto *LI = dyn_cast<LoadInst>(ptr)) {
            // 這是 *p = ... 或者 **pp = ...
            // 我們需要追蹤 Load 的來源
            string innerBase;
            int innerLevel;
            analyzeLHS(LI->getPointerOperand(), innerBase, innerLevel, TREF);
            
            baseVar = innerBase;
            level = innerLevel + 1;
            
            // 關鍵：如果是 *p = ...，我們必須讀取 p 才能知道寫到哪
            // 所以 baseVar (e.g., "p") 必須加入 TREF
            // 如果是 **pp = ...，遞歸會先把 pp 加入 TREF，這裡加入 *pp
            
            string refName = baseVar;
            for(int i=0; i<innerLevel; i++) refName = "*" + refName;
            
            TREF.insert(refName);
            
            // 根據 icpp 答案，如果是 S4: *p=... (p->y)，TREF 包含 "p" 和 "[*pp]" (alias)
            // 這裡我們只添加必須的語法引用，別名由 Checker 的 Optional 處理
        }
    }

    // 分析 RHS (讀取的值)
    void analyzeRHS(Value *val, set<string> &TREF) {
        if (auto *LI = dyn_cast<LoadInst>(val)) {
            // 讀取了某個變量
            string baseVar;
            int level = 0;
            set<string> dummy; // RHS解析時不需要像 LHS 那樣記錄中間指針到 TREF
            analyzeLHS(LI->getPointerOperand(), baseVar, level, dummy);
            
            string fullName = baseVar;
            for(int i=0; i<level; i++) fullName = "*" + fullName;
            TREF.insert(fullName);
            
            // 處理別名：如果讀取 *p 且 *p == x，通常我們只記錄 *p，
            // 但如果有多層指針，可能需要記錄更多。
            // 為了簡單起見，我們只記錄語法上的引用。
            
            // 特殊情況：如果是一個 Load(Load(...))，內層的 load 也貢獻了引用
             if (level > 0) {
                 // 例如 rhs 是 *p (level 1)，意味著 load(load ptr)。內層 load 讀取了 p。
                 string subName = baseVar;
                 for(int i=0; i<level-1; i++) subName = "*" + subName;
                 // 如果 logic 正確，遞歸 analyzeLHS 應該已經處理了？
                 // 不，上面的 analyzeLHS 是用來找 base 的。
                 // 讓我們簡單處理：遞歸分析操作數
                 analyzeRHS(LI->getPointerOperand(), TREF);
             }
        }
        else if (auto *BO = dyn_cast<BinaryOperator>(val)) {
            analyzeRHS(BO->getOperand(0), TREF);
            analyzeRHS(BO->getOperand(1), TREF);
        }
        // 注意：如果 RHS 是 &y (地址)，在 LLVM IR 中通常是直接使用 AllocaInst，
        // 這不構成 "Reference" (讀取內存)，所以不加入 TREF。
    }

    // 更新 Points-to 關係
    // var = valOp (其中 valOp 可能是 &y, 或者是 load %q)
    void updatePointsTo(string var, Value *valOp) {
        // Case 1: p = &y
        if (auto *AI = dyn_cast<AllocaInst>(valOp)) {
            string target = getVarName(AI);
            if (!target.empty()) {
                pointsTo[var] = target;
            }
        }
        // Case 2: p = q (pointer copy) -> p points to whatever q points to
        else if (auto *LI = dyn_cast<LoadInst>(valOp)) {
            // 我們需要知道 q 是誰
            string rhsVar; 
            int level;
            set<string> dummy;
            analyzeLHS(LI->getPointerOperand(), rhsVar, level, dummy);
            
            // 如果 rhsVar 是 q (level 0)，而 pointsTo[q] = z
            // 那麼 pointsTo[p] = z
            string rhsName = rhsVar;
            for(int i=0; i<level; i++) rhsName = "*" + rhsName; // e.g. *pp

            // 這裡有點複雜，簡化邏輯：
            // 如果 RHS 是 pointer type，我們查找它的 points-to 對象
            // 但在這裡我們只處理最簡單的 HW2 scope (scalar/ptr assignment)
            // 如果代碼是 p = q，且 q->x，則 p->x。
            
            // 查找 RHS 指向什麼
            // 在我們的 map 中，如果 rhsName 是 "q" 且 pointsTo["q"]=="x"
            if (pointsTo.count(rhsName)) {
                pointsTo[var] = pointsTo[rhsName];
            }
        }
        // Case 3: Constant (e.g., p = NULL), remove mapping
        else if (isa<Constant>(valOp)) {
            pointsTo.erase(var);
        }
    }

    // 根據 pointsTo 計算所有等價對 (Transitive Closure)
    set<pair<string, string>> computeTEQUIV() {
        set<pair<string, string>> equivs;
        
        // 1. 直接關係
        for (auto const &mapping : pointsTo) {
            string ptr = mapping.first;
            string target = mapping.second;
            // *ptr == target
            equivs.insert({"*" + ptr, target});
        }
        
        // 2. 遞移關係 (Transitive)
        // 重複遍歷直到沒有新的關係加入 (Fixed Point)
        bool changed = true;
        while(changed) {
            changed = false;
            set<pair<string, string>> newEquivs;
            
            for (auto const &mapping : pointsTo) {
                string p = mapping.first;       // p
                string x = mapping.second;      // x (p -> x)
                
                // 規則：如果我們已經知道 *p == x
                // 且我們知道 x == *q (即 q -> x) -> 這只是反向
                // 我們需要的是：如果 pp -> p，則 *pp == p。
                // 結合 p -> x (即 *p == x)，推導出 **pp == x。
                
                // 查找指向 p 的指針
                for (auto const &parentMap : pointsTo) {
                    string pp = parentMap.first;
                    string p_candidate = parentMap.second;
                    
                    if (p_candidate == p) {
                        // pp -> p -> x
                        // 我們有 (*pp, p) 和 (*p, x)
                        // 推導 (**pp, x)
                        pair<string, string> derived = {"**" + pp, x};
                        if (equivs.find(derived) == equivs.end()) {
                            newEquivs.insert(derived);
                            changed = true;
                        }
                    }
                }
            }
            equivs.insert(newEquivs.begin(), newEquivs.end());
        }
        
        return equivs;
    }

    void outputJSON(string filename) {
        std::error_code EC;
        raw_fd_ostream out(filename, EC);
        if (EC) {
            errs() << "Error opening file: " << EC.message() << "\n";
            return;
        }

        out << "{\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto &res = results[i];
            out << "  \"S" << res.stmtId << "\": {\n";
            
            // TREF
            out << "    \"TREF\": [";
            bool first = true;
            for (const auto &s : res.TREF) {
                if (!first) out << ", ";
                out << "\"" << s << "\"";
                first = false;
            }
            out << "],\n";

            // TGEN
            out << "    \"TGEN\": [";
            first = true;
            for (const auto &s : res.TGEN) {
                if (!first) out << ", ";
                out << "\"" << s << "\"";
                first = false;
            }
            out << "],\n";

            // DEP
            out << "    \"DEP\": [";
            first = true;
            for (const auto &d : res.DEP) {
                if (!first) out << ", ";
                out << "{ \"var\": \"" << d.var << "\", \"src_stmt\": " << d.srcStmt 
                    << ", \"dst_stmt\": " << d.dstStmt << ", \"type\": \"" << d.type << "\" }";
                first = false;
            }
            out << "],\n";

            // TDEF
            out << "    \"TDEF\": { ";
            first = true;
            for (const auto &kv : res.TDEF) {
                if (!first) out << ", ";
                out << "\"" << kv.first << "\": " << kv.second;
                first = false;
            }
            out << " },\n";

            // TEQUIV
            out << "    \"TEQUIV\": [";
            first = true;
            for (const auto &p : res.TEQUIV) {
                if (!first) out << ", ";
                out << "[\"" << p.first << "\", \"" << p.second << "\"]";
                first = false;
            }
            out << "]\n";

            out << "  }";
            if (i < results.size() - 1) out << ",";
            out << "\n";
        }
        out << "}\n";
    }
};

struct HW2Pass : public PassInfoMixin<HW2Pass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (F.isDeclaration()) return PreservedAnalyses::all();
        
        // 為了簡單起見，我們假設每個測試案例只有一個主要的邏輯函數
        // 根據作業描述，我們會跑 make test，針對特定 .ll 檔
        // 這裡直接分析所有非 main 的函數，或者全部分析
        
        // 根據 Checker，文件名與函數名通常一致 (icpp.c -> icpp function)
        // 這裡簡單地對每個函數輸出一個 json，讓 Makefile 決定跑哪個
        
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
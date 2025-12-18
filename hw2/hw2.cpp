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

struct VarTree {
    std::string name;
    int derefLevel; 
    
    VarTree(const std::string& n = "", int level = 0) 
        : name(n), derefLevel(level) {}
    
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

struct Dependence {
    std::string var;
    int srcStmt;
    int dstStmt;
    std::string type;  // flow or output
    
    bool operator<(const Dependence& other) const {
        if (var != other.var) return var < other.var;
        if (srcStmt != other.srcStmt) return srcStmt < other.srcStmt;
        if (dstStmt != other.dstStmt) return dstStmt < other.dstStmt;
        return type < other.type;
    }
};

struct EquivPair {
    std::string first;   // *p
    std::string second;  // x
    
    bool operator<(const EquivPair& other) const {
        if (first != other.first) return first < other.first;
        return second < other.second;
    }
};

struct StmtAnalysis {
    int stmtNum;
    std::set<std::string> TREF;
    std::set<std::string> TGEN;
    std::set<Dependence> DEP; 
    std::map<std::string, int> TDEF;
    std::set<EquivPair> TEQUIV; 
};

class PointerAnalyzer {
private:

    std::map<std::string, std::string> pointsTo;

    std::map<std::string, int> currentTDEF;
    
    std::set<EquivPair> currentTEQUIV;
    
    std::vector<StmtAnalysis> results;
    
    std::map<Value*, std::string> valueNames;
    
    int stmtCounter = 0;

public:
    // Get variable name
    std::string getVarName(Value* V) {
        if (!V) return "";
        
        // If already mapped, return directly
        if (valueNames.count(V)) {
            return valueNames[V];
        }
        
        // Try to get name from LLVM
        if (V->hasName()) {
            std::string name = V->getName().str();
            valueNames[V] = name;
            return name;
        }
        
        // For alloca instructions, try to get debug info
        if (auto* AI = dyn_cast<AllocaInst>(V)) {
            // Check if there is debug info
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
    
    // Analyze store instruction
    // store instruction: store <value>, <pointer>
    // Means storing value into the location pointed by pointer
    void analyzeStore(StoreInst* SI, int stmtNum) {
        StmtAnalysis analysis;
        analysis.stmtNum = stmtNum;
        
        Value* valueOp = SI->getValueOperand();   // The value being stored
        Value* ptrOp = SI->getPointerOperand();   // The target address for storage
        
        // Analyze write target (TGEN)
        std::set<std::string> genSet;
        std::string targetVar = analyzeWriteTarget(ptrOp, genSet, analysis.TREF);
        
        // Analyze read variables (TREF)
        analyzeReadValue(valueOp, analysis.TREF);
        
        analysis.TGEN = genSet;
        
        // Compute dependencies
        computeDependences(analysis);
        
        // Update TDEF
        for (const auto& var : analysis.TGEN) {
            currentTDEF[var] = stmtNum;
        }
        analysis.TDEF = currentTDEF;
        
        // Update equivalence relations (if it's a pointer assignment)
        updateEquivalences(SI, stmtNum);
        analysis.TEQUIV = currentTEQUIV;
        
        results.push_back(analysis);
    }
    
    // Analyze write target, return base variable name, and fill genSet
    std::string analyzeWriteTarget(Value* ptr, std::set<std::string>& genSet, 
                                   std::set<std::string>& refSet) {
        // Case 1: Directly an alloca (e.g., store ... %x)
        if (auto* AI = dyn_cast<AllocaInst>(ptr)) {
            std::string name = getVarName(AI);
            if (!name.empty()) {
                genSet.insert(name);
                return name;
            }
        }
        
        // Case 2: Result of a load (e.g., store ... %1, where %1 = load %p)
        // This represents indirect write: *p = ...
        if (auto* LI = dyn_cast<LoadInst>(ptr)) {
            Value* loadPtr = LI->getPointerOperand();
            
            // Single-level indirect: *p = ...
            if (auto* AI = dyn_cast<AllocaInst>(loadPtr)) {
                std::string ptrName = getVarName(AI);
                if (!ptrName.empty()) {
                    // Need to read p to know where to write
                    refSet.insert(ptrName);
                    
                    // If p is equivalent to some expression (like *pp), also add to refSet
                    for (const auto& eq : currentTEQUIV) {
                        if (eq.second == ptrName) {
                            refSet.insert(eq.first);
                        } else if (eq.first == ptrName) {
                            refSet.insert(eq.second);
                        }
                    }
                    
                    // TGEN includes *p
                    std::string derefName = "*" + ptrName;
                    genSet.insert(derefName);
                    
                    // If *p is equivalent to some variable, also add to TGEN
                    addEquivalentVars(derefName, genSet);
                    
                    return derefName;
                }
            }
            
            // Double-level indirect: **pp = ... (ptr is result of load (load %pp))
            if (auto* LI2 = dyn_cast<LoadInst>(loadPtr)) {
                Value* loadPtr2 = LI2->getPointerOperand();
                if (auto* AI = dyn_cast<AllocaInst>(loadPtr2)) {
                    std::string ppName = getVarName(AI);
                    if (!ppName.empty()) {
                        refSet.insert(ppName);
                        refSet.insert("*" + ppName);
                        
                        // Add equivalent variables of *pp (like p) to refSet
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
    
    // Analyze read value
    void analyzeReadValue(Value* val, std::set<std::string>& refSet) {
        // If it's a constant, no processing needed
        if (isa<Constant>(val)) return;
        
        // If it's an address-of operation (& operator), it's not a reference
        // In LLVM IR, &x is directly the result of alloca, no extra load
        
        // If it's a load instruction, track the variable being read
        if (auto* LI = dyn_cast<LoadInst>(val)) {
            analyzeLoad(LI, refSet);
        }
        
        // If it's a binary operation, recursively analyze operands
        if (auto* BO = dyn_cast<BinaryOperator>(val)) {
            analyzeReadValue(BO->getOperand(0), refSet);
            analyzeReadValue(BO->getOperand(1), refSet);
        }
    }
    
    // Analyze load instruction
    void analyzeLoad(LoadInst* LI, std::set<std::string>& refSet) {
        Value* ptr = LI->getPointerOperand();
        
        // Direct load from alloca: reading variable x
        if (auto* AI = dyn_cast<AllocaInst>(ptr)) {
            std::string name = getVarName(AI);
            if (!name.empty()) {
                refSet.insert(name);
                
                // If this variable is equivalent to some pointer expression, also add it
                // For example: if y is equivalent to *p, reading y also means reading *p
                for (const auto& eq : currentTEQUIV) {
                    if (eq.second == name) {
                        refSet.insert(eq.first);
                    }
                }
            }
            return;
        }
        
        // Indirect load: load (load %p) means reading *p
        if (auto* LI2 = dyn_cast<LoadInst>(ptr)) {
            Value* ptr2 = LI2->getPointerOperand();
            if (auto* AI = dyn_cast<AllocaInst>(ptr2)) {
                std::string ptrName = getVarName(AI);
                if (!ptrName.empty()) {
                    refSet.insert(ptrName);
                    std::string derefName = "*" + ptrName;
                    refSet.insert(derefName);
                    
                    // Add equivalent variables of *p (e.g., y)
                    for (const auto& eq : currentTEQUIV) {
                        if (eq.first == derefName) {
                            refSet.insert(eq.second);
                        } else if (eq.second == derefName) {
                            refSet.insert(eq.first);
                        }
                    }
                }
            }
            
            // Double-level indirect load: load (load (load %pp)) means reading **pp
            if (auto* LI3 = dyn_cast<LoadInst>(ptr2)) {
                Value* ptr3 = LI3->getPointerOperand();
                if (auto* AI = dyn_cast<AllocaInst>(ptr3)) {
                    std::string ppName = getVarName(AI);
                    if (!ppName.empty()) {
                        refSet.insert(ppName);
                        refSet.insert("*" + ppName);
                        std::string derefName = "**" + ppName;
                        refSet.insert(derefName);
                        
                        // Add equivalent variables of **pp
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
    
    // Add equivalent variables to the set
    void addEquivalentVars(const std::string& var, std::set<std::string>& varSet) {
        for (const auto& eq : currentTEQUIV) {
            if (eq.first == var) {
                varSet.insert(eq.second);
            } else if (eq.second == var) {
                varSet.insert(eq.first);
            }
        }
    }
    
    // Compute data dependencies
    void computeDependences(StmtAnalysis& analysis) {
        // Flow dependence: variables in TREF have definitions in TDEF
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
        
        // Output dependence: variables in TGEN have definitions in TDEF
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
    
    // Update equivalence relations
    void updateEquivalences(StoreInst* SI, int stmtNum) {
        Value* valueOp = SI->getValueOperand();
        Value* ptrOp = SI->getPointerOperand();
        
        // Check if it's a pointer assignment (value is address of some alloca)
        // p = &x in IR is: store i32* %x, i32** %p
        if (auto* AI = dyn_cast<AllocaInst>(valueOp)) {
            // valueOp is &x (address of x)
            std::string pointee = getVarName(AI);
            
            // Case 1: Direct assignment p = &x
            // ptrOp should be p's alloca
            if (auto* PtrAI = dyn_cast<AllocaInst>(ptrOp)) {
                std::string pointer = getVarName(PtrAI);
                
                if (!pointer.empty() && !pointee.empty()) {
                    // Establish p -> x points-to relation
                    pointsTo[pointer] = pointee;
                    
                    // Add equivalence relation: *p is equivalent to x
                    EquivPair eq;
                    eq.first = "*" + pointer;
                    eq.second = pointee;
                    currentTEQUIV.insert(eq);
                    
                    // Propagate equivalence relations
                    propagateEquivalences();
                }
            }
            
            // Case 2: Indirect assignment *pp = &y (ptrOp is result of load %pp)
            // This means the pointer that pp points to now points to y
            if (auto* LI = dyn_cast<LoadInst>(ptrOp)) {
                Value* loadPtr = LI->getPointerOperand();
                if (auto* PtrAI = dyn_cast<AllocaInst>(loadPtr)) {
                    std::string ppName = getVarName(PtrAI);
                    
                    if (!ppName.empty() && !pointee.empty()) {
                        // pp points to p, so *pp = &y means p = &y
                        // Find what pp points to (i.e., what *pp is equivalent to)
                        std::string targetPointer;
                        for (const auto& eq : currentTEQUIV) {
                            if (eq.first == "*" + ppName) {
                                targetPointer = eq.second;
                                break;
                            }
                        }
                        
                        if (!targetPointer.empty()) {
                            // Update targetPointer -> pointee points-to relation
                            pointsTo[targetPointer] = pointee;
                            
                            // Clear old equivalence relations and rebuild
                            rebuildEquivalences();
                        }
                    }
                }
            }
        }
    }
    
    // Rebuild all equivalence relations
    void rebuildEquivalences() {
        currentTEQUIV.clear();
        
        // Build basic equivalence relations based on pointsTo
        for (const auto& pt : pointsTo) {
            EquivPair eq;
            eq.first = "*" + pt.first;
            eq.second = pt.second;
            currentTEQUIV.insert(eq);
        }
        
        // Propagate equivalence relations
        propagateEquivalences();
    }
    
    // Propagate equivalence relations
    // If pp -> p and p -> x, then **pp is equivalent to x
    void propagateEquivalences() {
        bool changed = true;
        while (changed) {
            changed = false;
            
            for (const auto& pt1 : pointsTo) {
                // pt1: pointer1 -> pointee1
                // If pointee1 is also a pointer and points to pointee2
                if (pointsTo.count(pt1.second)) {
                    std::string pointer1 = pt1.first;
                    std::string pointer2 = pt1.second;
                    std::string pointee2 = pointsTo[pointer2];
                    
                    // *pp is equivalent to p
                    EquivPair eq1;
                    eq1.first = "*" + pointer1;
                    eq1.second = pointer2;
                    if (currentTEQUIV.find(eq1) == currentTEQUIV.end()) {
                        currentTEQUIV.insert(eq1);
                        changed = true;
                    }
                    
                    // **pp is equivalent to *p (which is equivalent to pointee2)
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
    
    // Output JSON format result
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
    
    void analyzeFunction(Function& F) {
        stmtCounter = 0;
        currentTDEF.clear();
        currentTEQUIV.clear();
        pointsTo.clear();
        results.clear();
        valueNames.clear();
        
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* AI = dyn_cast<AllocaInst>(&I)) {
                    getVarName(AI);
                }
            }
        }
        
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* SI = dyn_cast<StoreInst>(&I)) {
                    stmtCounter++;
                    analyzeStore(SI, stmtCounter);
                }
            }
        }
        
        // Output results
        std::string funcName = F.getName().str();
        outputJSON(funcName + ".json");
    }
};

struct HW2Pass : public PassInfoMixin<HW2Pass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        
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
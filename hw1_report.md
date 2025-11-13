# hw1_114062545_report

## 1. 作業概述

本作業實作**基於 Diophantine 方程求解法**進行相依性分析，並**採用 LLVM 17.0.2 的現代 Pass Manager 架構（PassInfoMixin + CRTP）**。資料結構方面則**使用了 LLVM ADT 中的 SmallVector** 來儲存陣列存取資訊。

相依性分析的情境，我以下方這個迴圈作為舉例：

```c
for (i = 4; i < 20; i++) {
    A[i] = C[i];        // 先寫入 A[i]
    B[i] = A[i - 4];    // 再讀 A[i-4]
}
```

當 `i=8` 時寫入 `A[8]`，然後當 `i=12` 時讀 `A[8]`（因為 `12-4=8`），這就產生了相依性。

要找出三種相依性：

- **Flow Dependence**: Write → Read（前面的寫，後面來讀）
- **Anti Dependence**: Read → Write（前面讀，後面寫）
- **Output Dependence**: Write → Write（前面寫，後面又寫）

分析範圍涵蓋格式為 `i + c` 和 `c * i + d` 的一維陣列索引表達式，其中 `i` 為迴圈計數變數，`c` 和 `d` 為常數。

---

## 2. 使用 Diophantine 方程求解相依性

### 2.1 核心演算法

本實作 **基於 Diophantine 方程求解法（based on and inspired by** **https://drive.google.com/file/d/1R9UMQL_K4Qj_YAlq1e9BtA4VxLtDUydr/view）** 來判定資料相依性，而非暴力枚舉。

### 步驟 1: 建立 Diophantine 方程

對於兩個陣列存取 `S_src` 和 `S_dst`：

- `S_src`: `A[c1*i1 + d1]`
- `S_dst`: `A[c2*i2 + d2]`

當兩者存取同一記憶體位置時，須滿足：

```
c1*i1 + d1 = c2*i2 + d2
```

轉換為標準 Diophantine 方程形式：

```
a*x + b*y = C
其中 a = c1, b = -c2, C = d2 - d1
```

### 步驟 2: GCD 可解性測試

使用 GCD (最大公因數) 判斷方程是否有整數解：

```cpp
long long gcd(long long a, long long b) {
    while (b) {
        a %= b;
        std::swap(a, b);
    }
    return a;
}
```

**可解性條件**: `C % gcd(a, b) == 0`

若不滿足此條件，則兩個陣列存取永遠不會存取同一記憶體位置，因此不存在相依性。

**p.s. [hw1.cpp - lin397] 特殊情況處理 (g = 0)**：

⇒ 基於 g 定義為 (a, b) 的最大公因數：`long long g = gcd(a, b);`

當兩個索引都是常數時（`c1 = 0` 且 `c2 = 0`），`gcd(a, b) = 0`。此時只需檢查 `d1 == d2` 即可判定是否存在相依性。

### 步驟 3: Extended Euclidean Algo 求特解

使用 extended Euclidean algo 求解 `a*x' + b*y' = gcd(a, b)`：

```cpp
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
```

得到一組特解後，原方程的特解為：

```
i1_0 = x' * (C / g)
i2_0 = y' * (C / g)
```

### 步驟 4: 一般解與邊界條件

Diophantine 方程的一般解為：

```
i1(t) = i1_0 + t * (b/g)
i2(t) = i2_0 - t * (a/g)
```

其中 `t` 為任意整數參數。

接著需要求解 `t` 的有效範圍，需同時滿足三個邊界條件：

1. **迴圈邊界 1**: `loopStart ≤ i1(t) < loopEnd`
2. **迴圈邊界 2**: `loopStart ≤ i2(t) < loopEnd`
3. **時間順序約束 (Execution Order Constraint)**:
    - 若 `S_src.stmtNum < S_dst.stmtNum`：允許 `i1 ≤ i2` (loop-independent 或 loop-carried)
    - 若 `S_src.stmtNum ≥ S_dst.stmtNum`：必須 `i1 < i2` (僅 loop-carried)

### 步驟 5: 求解 t 的範圍

實作 `solveBounds` 函式來處理不等式 `L ≤ C + t*S < U`：

```cpp
void solveBounds(long long L, long long U, long long C, long long S,
                 long long &t_min, long long &t_max) {
    if (S == 0) {
        if (L <= C && C < U) return;
        t_min = 1; t_max = 0;
        return;
    }

    long long R_min = L - C;
    long long R_max_inclusive = U - C - 1;

    if (S > 0) {
        t_min_new = (R_min > 0 && R_min % S != 0) ? (R_min / S) + 1 : (R_min / S);
        t_max_new = (R_max_inclusive < 0 && R_max_inclusive % S != 0) ?
                    (R_max_inclusive / S) - 1 : (R_max_inclusive / S);
    } else {
        t_max_new = (R_min > 0 && R_min % S != 0) ? (R_min / S) - 1 : (R_min / S);
        t_min_new = (R_max_inclusive < 0 && R_max_inclusive % S != 0) ?
                    (R_max_inclusive / S) + 1 : (R_max_inclusive / S);
    }

    t_min = std::max(t_min, t_min_new);
    t_max = std::min(t_max, t_max_new);
}
```

### 步驟 6: 生成所有相依性 pair

對於有效範圍內的每個 `t`，計算對應的 `(i1, i2)` 並記錄相依性：

```cpp
for (long long t = t_min; t <= t_max; t++) {
    Dependence dep;
    dep.array = S_src.arrayName;
    dep.src_stmt = S_src.stmtNum;
    dep.src_idx = i1_0 + t * step_i1;
    dep.dst_stmt = S_dst.stmtNum;
    dep.dst_idx = i2_0 + t * step_i2;
    depSet->insert(dep);
}
```

### 2.2 使用 Diophantine 方程優勢

相較於一般的暴力枚舉，Diophantine 方程求解法具有以下優勢：

1. **理論正確性**：基於數論的嚴謹數學基礎
2. **完整性**：能找出所有可能的相依性
3. **效率**：對於大範圍迴圈，避免了 O(n²) 暴力枚舉的開銷
4. **可擴展性**：容易擴展到更複雜的索引表達式

---

## 3. LLVM Pass 實作架構

### 3.1 PassInfoMixin 模式與 CRTP (Bonus 1)

這邊採用 LLVM 的現代 Pass 架構，Pass 類別定義為：

```cpp
class HW1Pass : public PassInfoMixin<HW1Pass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
    // ... 
};
```

這裡使用了兩個重要的 C++ 設計模式：**Curiously Recurring Template Pattern (CRTP)** 和 **Mixin Pattern**。

### **Curiously Recurring Template Pattern (CRTP)**

**LLVM 為什麼要用 CRTP：**

1. **靜態多型的實現**
    - CRTP 在編譯時就決定好要呼叫哪個函式，不像虛函式要在執行時才查表
    - **消除執行期成本**：不需要虛函式表 (vtable) 查詢
    - **內聯優化機會**：編譯器可以把整個函式呼叫都內聯展開
    - 對編譯器這種注重效能的系統來說非常重要
2. **編譯期型別檢查**
    - 父類可以在編譯時就直接存取子類的成員
    - 型別錯誤在編譯時就會被抓到
    - 減少執行時的型別轉換和檢查
3. **Zero-cost 抽象設計**
    - 不會增加物件的記憶體大小（由於不使用虛擬機制，物件實例中無需虛擬指標 vptr ）
    - 沒有執行期負擔
    - 符合 C++ 和 LLVM 的效能至上的理念

### **Mixin Pattern**

從 `PassInfoMixin` 這個命名即可得知，它是一個 `Mixin` 類別，其目的在於提供可整合至 Pass 類別的 generic functionality。

**Mixin Pattern 的設計特性：**

1. **功能提供機制**：`PassInfoMixin` 幫 Pass 類別添加必要的 Infra
    - Pass 的型別識別資訊
    - Pass 的名稱和 ID
    - 跟 PassManager 整合的介面
2. **組合導向設計**：
    - 透過模板參數和繼承來整合通用功能
    - 避免複雜的繼承層次
    - 每個 Pass 類別可以選擇性地納入 (opt-in) 所需的 Mixin 功能
3. **模組化架構**：
    - Pass 的共用邏輯包裝在 Mixin 裡
    - Pass 開發者只要專注在較為重要的分析邏輯（`run` 方法）
    - Infra 由框架自動提供

### **為什麼 LLVM 採用這個模式？**

1. **Pass 系統的演進**
    
    **舊的 Legacy Pass Manager**：
    
    ```cpp
    struct OldPass : public FunctionPass {
        virtual bool runOnFunction(Function &F);  // 虛函式
    };
    ```
    
    - 使用虛函式繼承
    - 動態多型會有 vtable 查表的開銷
    - Pass 之間的相依性管理複雜
    
    **新的 Pass Manager** (使用 CRTP + Mixin)：
    
    ```cpp
    class NewPass : public PassInfoMixin<NewPass> {
        PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
    };
    ```
    
    - 編譯期多型，zero cost
    - 更清晰的相依性管理（透過 `FunctionAnalysisManager`）
    - 更靈活的 Pass 管線組合
2. **效能考量**
    - LLVM 是對效能要求很高的 Infra
    - Pass 可能會被呼叫非常多次，虛函式的開銷會累積
    - CRTP 允許編譯器完全優化 Pass 的呼叫
3. **類型安全與可維護性**
    - 編譯期的型別檢查可以減少執行期錯誤
    - Pass 的介面在編譯時就被驗證了
    - 重構更安全，開發工具的支援也比較好
4. **擴展性與靈活性**
    - 新的 Pass 只需繼承 `PassInfoMixin` 並實作 `run` 方法就好
    - 可以輕鬆添加不同的功能模組
    - Pass 管線的組合更有彈性

---

### 3.2 ADT 資料結構使用 (Bonus 2)

本作業使用了 LLVM 提供的 ADT (Abstract Data Type) 資料結構：

### **SmallVector 的使用**

```cpp
llvm::SmallVector<ArrayAccess> arrayAccesses;
```

**為什麼選用 SmallVector：**

1. **小型優化 (Small Size Optimization, SSO)**
    - 對小型集合（預設容量內的元素數量），資料直接放在 stack 上
    - 避免 heap 記憶體配置，減少 malloc/free 的開銷
    - 當元素數量超過預設容量才會動態配置到 heap
2. **效能優勢**
    - **快取友善**：連續記憶體配置，提高 CPU cache 命中率
    - **減少記憶體碎片**：stack 配置不會產生 heap 碎片
    - **避免動態配置開銷**：小型資料不需要 `malloc`/`new`
3. **相容性和易用性**
    - 介面跟 `std::vector` 完全相容
    - 可以無縫替換 STL 容器
    - 支援範圍迭代器和現代 C++ 特性

### **實際應用分析 (usage experience)**

根據執行結果，本作業處理的測試案例資料量如下：

**test1.c 分析結果：**

```
Array accesses:
  Load from C[1*i + 0] (S1)
  Store to A[1*i + 0] (S1)
  Load from A[1*i + -4] (S2)
  Store to B[1*i + 0] (S2)
Total accesses found: 4
```

- 共 4 個陣列存取（2 個 Load + 2 個 Store）
- 產生 12 個 flow dependences

**test2.c 分析結果：**

```
Array accesses:
  Load from C[1*i + 0] (S1)
  Store to A[1*i + 0] (S1)
  Load from A[3*i + -4] (S2)
  Store to D[1*i + 0] (S2)
  Load from C[2*i + 0] (S3)
  Store to D[1*i + -1] (S3)
Total accesses found: 6
```

- 共 6 個陣列存取（3 個 Load + 3 個 Store）
- 產生 1 個 flow dependence + 5 個 anti dependences + 17 個 output dependences

兩個測試案例的陣列存取數量都在 10 個以內，非常適合使用 `SmallVector` ！

### 使用 ADT 的心得

透過這次實作，我實際體會到在編譯器這種高頻呼叫的場景下，針對小數據量選擇 `SmallVector` 比起通用的 `std::vector` 是個更優的選擇。由於本作業的測試案例資料量皆小於 10（test2.c 也才六個），剛好能發揮 `SmallVector` 的優勢：

- **避免 Heap 開銷**：資料完全在 Stack 上配置，省去了動態記憶體管理的成本。
- **提升 Cache 效能**：連續記憶體配置對 CPU cache 更友善，這讓我更理解 LLVM 編碼規範背後的效能考量。

---

## 4. Reference

- LLVM Project: [https://llvm.org/](https://llvm.org/)
- LLVM Language Reference Manual: [https://llvm.org/docs/LangRef.html](https://llvm.org/docs/LangRef.html)
- LLVM Loop Documentation: [https://llvm.org/doxygen/classllvm_1_1Loop.html](https://llvm.org/doxygen/classllvm_1_1Loop.html)
- Data Dependence Analysis (Course Material): [https://drive.google.com/file/d/1R9UMQL_K4Qj_YAlq1e9BtA4VxLtDUydr/view](https://drive.google.com/file/d/1R9UMQL_K4Qj_YAlq1e9BtA4VxLtDUydr/view)
- LLVM ADT Documentation: [https://llvm.org/docs/ProgrammersManual.html#picking-the-right-data-structure-for-a-task](https://llvm.org/docs/ProgrammersManual.html#picking-the-right-data-structure-for-a-task)
- Padua, D. A., & Wolfe, M. J. (1986). Advanced compiler optimizations for supercomputers. Communications of the ACM, 29(12), 1184-1201.
// test3.c - Edge Cases Test
// 測試負係數的 Diophantine 求解

int main() {
    int i;
    int A[25], B[25];
    
    for (i = 2; i < 12; i++) {
        A[15 - i] = i;         // S1: Write A[15-i]
        B[i] = A[2 * i - 3];   // S2: Read A[2*i-3], Write B
    }
    
    return 0;
}
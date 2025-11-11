int main(){
    int i;
    int A[20], B[20], C[20];
    for (i = 4; i < 20; i++) {
        A[i] = C[i];      // Statement 1 (S1)
        B[i] = A[i - 4];  // Statement 2 (S2)
    }
    return 0;
}
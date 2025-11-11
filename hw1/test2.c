int main(){
    int i;
    int A[40], C[40], D[40];
    for (i = 2; i < 20; i++) {
        A[i] = C[i];        // Statement 1 (S1)
        D[i] = A[3*i - 4];  // Statement 2 (S2)
        D[i - 1] = C[2*i];  // Statement 3 (S3)
    }
    return 0;
}
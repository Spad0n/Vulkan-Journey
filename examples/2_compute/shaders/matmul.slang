struct PushConstants {
    int* A;
    int* B;
    int* C;
    uint N;
};

[[vk::push_constant]] PushConstants pc;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint row = threadId.y;
    uint col = threadId.x;
    uint N = pc.N;

    if (row < N && col < N) {
        int sum = 0;
        for (uint k = 0; k < N; k++) {
            sum += pc.A[row * N + k] * pc.B[k * N + col];
        }
        pc.C[row * N + col] = sum;
    }
}

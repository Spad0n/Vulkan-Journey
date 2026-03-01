struct PushConstants {
    int* A;
    int* result;
    uint N;
};

[[vk::push_constant]] PushConstants pc;

groupshared int cache[256];

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 threadIdx : SV_GroupThreadID, uint3 dispatchIdx : SV_DispatchThreadID) {
    uint tid = threadIdx.x;
    uint gid = dispatchIdx.x;
    uint N = pc.N;

    if (gid < N) {
        cache[tid] = pc.A[gid * N + gid];
    } else {
        cache[tid] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            cache[tid] += cache[tid + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid == 0) {
        InterlockedAdd(pc.result[0], cache[0]);
    }
}

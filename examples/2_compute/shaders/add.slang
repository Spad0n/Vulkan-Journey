struct PushConstants {
    float *data;
};

[[vk::push_constant]] PushConstants pc;

[shader("compute")][numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    if (threadId.x < 1024) {
        pc.data[threadId.x] += 10.0f;
    }
}

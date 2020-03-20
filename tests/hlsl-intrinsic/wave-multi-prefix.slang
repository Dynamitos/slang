//DISABLE_TEST(compute):COMPARE_COMPUTE_EX:-cpu -compute 
//DISABLE_TEST(compute):COMPARE_COMPUTE_EX:-slang -compute
// We need SM6.5 for these tests
// Disable because version of dxc we are currently using doesn't support SM6.5
//DISABLE_TEST(compute):COMPARE_COMPUTE_EX:-slang -compute -dx12 -use-dxil -profile sm_6_5
// Disabled because we don't have GLSL intrinsics for these it seems 
//DISABLE_TEST(compute, vulkan):COMPARE_COMPUTE_EX:-vk -compute
//TEST(compute):COMPARE_COMPUTE_EX:-cuda -compute

//TEST_INPUT:ubuffer(data=[0 0 0 0 0 0 0 0], stride=4):out,name outputBuffer
RWStructuredBuffer<int> outputBuffer;

[numthreads(8, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int idx = int(dispatchThreadID.x);
    
    int value = 0;
    
    uint4 mask = WaveMatch(true);
    
    // Scalar
    value += WaveMultiPrefixSum(1 << idx, mask);
    
    outputBuffer[idx] = value;
}
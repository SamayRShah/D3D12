
cbuffer ExternalData : register(b0)
{
    uint vsVertexBufferIndex;
    uint vsCBIndex;
    uint psSkyboxIndex;
}

// texture sampler
SamplerState BasicSampler : register(s0);

struct VertexToPixel_Sky
{
    float4 screenPosition : SV_POSITION;
    float3 sampleDir : DIRECTION;
};

// entry
float4 main(VertexToPixel_Sky input) : SV_TARGET
{
    TextureCube SkyTexture = ResourceDescriptorHeap[psSkyboxIndex];
    return SkyTexture.Sample(BasicSampler, input.sampleDir);
}
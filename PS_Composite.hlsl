cbuffer DrawData : register(b0)
{
    uint albedoIndex;
    //uint normalIndex;
    //uint materialIndex;
    //uint depthIndex;
}

// Defines the input to this pixel shader
struct VertexToPixel
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Sampler
SamplerState BasicSampler : register(s0);

// Entry point for this pixel shader
float4 main(VertexToPixel input) : SV_TARGET
{
    Texture2D<float4> albedoTex = ResourceDescriptorHeap[albedoIndex];
    // exture2D<float4> normalTex = ResourceDescriptorHeap[normalIndex];
    // exture2D<float4> materialTex = ResourceDescriptorHeap[materialIndex];
    // exture2D<float> depthTex = ResourceDescriptorHeap[depthIndex];
    
    float2 uv = input.uv;

    float3 albedo = albedoTex.Sample(BasicSampler, uv).rgb;
    // float roughness = materialTex.Sample(BasicSampler, uv).g;

    return float4(albedo.rgb, 1);
}
cbuffer DrawData : register(b0)
{
    uint albedoIndex;
    uint normalIndex;
    uint materialIndex;
    uint depthIndex;
    uint lightingIndex;
    uint sdfIndex;
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
    // Sample G-buffer textures
    Texture2D albedoTex = ResourceDescriptorHeap[albedoIndex];
    Texture2D normalTex = ResourceDescriptorHeap[normalIndex];
    Texture2D materialTex = ResourceDescriptorHeap[materialIndex];
    Texture2D depthTex = ResourceDescriptorHeap[depthIndex];
    Texture2D lightingTex = ResourceDescriptorHeap[lightingIndex];
    Texture2D sdfTexture = ResourceDescriptorHeap[sdfIndex];
    
    float4 sdf = sdfTexture.SampleLevel(BasicSampler, input.uv, 0);
    float4 albedo = albedoTex.SampleLevel(BasicSampler, input.uv, 0);
    float4 normal = normalTex.SampleLevel(BasicSampler, input.uv, 0);
    float4 material = materialTex.SampleLevel(BasicSampler, input.uv, 0);
    float depth = depthTex.SampleLevel(BasicSampler, input.uv, 0).r;
    float4 lighting = lightingTex.SampleLevel(BasicSampler, input.uv, 0);

    // Determine if this pixel is sky (far plane)
    bool isSky = depth >= 1.0f;

    // combine lighting with albedo
    float3 finalColor = isSky ? albedo.rgb : albedo.rgb * lighting.rgb;
    finalColor += sdf.rgb;

    return float4(finalColor, 1.0f);
}

#include "Lighting.hlsli"

cbuffer BindlessData : register(b0)
{
    uint vsVertexBufferIndex;
    uint vsPerFrameCBIndex;
    uint vsPerObjectCBIndex;
    uint psPerFrameCBIndex;
    uint psPerObjectCBIndex;
}

struct PSPerFrameData
{
    float3 cameraPosition;
    float pad;
};

struct PSPerObjectData
{
    // constants
    float3 color;
    float roughness;
    float metalness;
    float3 pad;
    
    // textures
    uint albedoIndex;
    uint normalMapIndex;
    uint roughnessIndex;
    uint metalnessIndex;
    float2 uvScale;
    float2 uvOffset;
};

// Struct representing the data we expect to receive from earlier pipeline stages
// - Should match the output of our corresponding vertex shader
// - The name of the struct itself is unimportant
// - The variable names don't have to match other shaders (just the semantics)
// - Each variable must have a semantic, which defines its usage
struct VertexToPixel
{
    float4 screenPosition : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 worldPos : POSITION;
};

struct PS_Output
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 Material : SV_TARGET2;
    float Depth : SV_TARGET3;
};

// texture sampelr
SamplerState BasicSampler : register(s0);

// --------------------------------------------------------
// The entry point (main method) for our pixel shader
// 
// - Input is the data coming down the pipeline (defined by the struct)
// - Output is a single color (float4)
// - Has a special semantic (SV_TARGET), which means 
//    "put the output of this into the current render target"
// - Named "main" because that's the default the shader compiler looks for
// --------------------------------------------------------
PS_Output main(VertexToPixel input) : SV_TARGET
{
    // get data from heap
    ConstantBuffer<PSPerFrameData> cbFrame = ResourceDescriptorHeap[psPerFrameCBIndex];
    ConstantBuffer<PSPerObjectData> cbObject = ResourceDescriptorHeap[psPerObjectCBIndex];
    
    Texture2D NormalMap = ResourceDescriptorHeap[cbObject.normalMapIndex];
    Texture2D MetalnessMap = ResourceDescriptorHeap[cbObject.metalnessIndex];
    
    // cleanup input data
    input.normal = normalize(input.normal);
    input.tangent = normalize(input.tangent);
    
    // adjust uv coords
    input.uv = input.uv * cbObject.uvScale + cbObject.uvOffset;
    
    // apply normal map
    if (cbObject.normalMapIndex != 0)
    {
        input.normal = NormalMapping(NormalMap, BasicSampler, input.uv, input.normal, input.tangent);
    }
    
    // Surface color with gamma correction
    float4 surfaceColor = float4(cbObject.color, 1);
    if (cbObject.albedoIndex != 0)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.albedoIndex];
        surfaceColor = tex.Sample(BasicSampler, input.uv);
        // surfaceColor = pow(surfaceColor, 2.2);
    }
    
    // roughness
    float roughness = cbObject.roughness;
    if (cbObject.roughnessIndex != 0)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.roughnessIndex];
        roughness = tex.Sample(BasicSampler, input.uv).r;
    }
    
    // metalness
    float metalness = cbObject.metalness;
    if (cbObject.metalnessIndex != 0)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.metalnessIndex];
        metalness = tex.Sample(BasicSampler, input.uv).r;
    }
       
    // Set up return struct
    PS_Output output;
    output.Color = float4(surfaceColor.rgb, 1.0f);
    output.Normal = float4(input.normal * 0.5f + 0.5f, 1.0f);
    output.Material = float4(roughness, metalness, 0, 1);
    output.Depth = input.screenPosition.z;
    return output;
}

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
    int lightCount;
    Light lights[MAX_LIGHTS];
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
	float4 screenPosition	: SV_POSITION;
    float2 uv				: TEXCOORD;
	float3 normal			: NORMAL;
	float3 tangent			: TANGENT;
	float3 worldPos			: POSITION;
};

// texture sampelr
SamplerState BasicSampler   : register(s0);

// --------------------------------------------------------
// The entry point (main method) for our pixel shader
// 
// - Input is the data coming down the pipeline (defined by the struct)
// - Output is a single color (float4)
// - Has a special semantic (SV_TARGET), which means 
//    "put the output of this into the current render target"
// - Named "main" because that's the default the shader compiler looks for
// --------------------------------------------------------
float4 main(VertexToPixel input) : SV_TARGET
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
    if(cbObject.normalMapIndex != -1)
    {
        input.normal = NormalMapping(NormalMap, BasicSampler, input.uv, input.normal, input.tangent);
    }
    
    // Surface color with gamma correction
    float4 surfaceColor = float4(cbObject.color, 1);
    if (cbObject.albedoIndex != -1)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.albedoIndex];
        surfaceColor = tex.Sample(BasicSampler, input.uv);
        surfaceColor = pow(surfaceColor, 2.2);
    }
    
    // roughness
    float roughness = cbObject.roughness;
    if (cbObject.roughnessIndex != -1)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.roughnessIndex];
        roughness = tex.Sample(BasicSampler, input.uv).r;
    }
    
    // metalness
    float metalness = cbObject.metalness;
    if (cbObject.metalnessIndex != -1)
    {
        Texture2D tex = ResourceDescriptorHeap[cbObject.metalnessIndex];
        metalness = tex.Sample(BasicSampler, input.uv).r;
    }
   
    float3 specColor = lerp(F0_NON_METAL, surfaceColor.rgb, metalness);
    
    // lighting
    float3 totalLight = float3(0, 0, 0);
    
    for (int i = 0; i < cbFrame.lightCount; i++)
    {
        Light light = cbFrame.lights[i];
        light.Direction = normalize(light.Direction);
        
        // Run the correct lighting calculation based on the light's type
        switch (light.Type)
        {
            case LIGHT_TYPE_DIRECTIONAL:
                totalLight += DirLightPBR(
                    light, input.normal, input.worldPos, 
                    cbFrame.cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;
            case LIGHT_TYPE_POINT:
                totalLight += PointLightPBR(
                    light, input.normal, input.worldPos, 
                    cbFrame.cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;

            case LIGHT_TYPE_SPOT:
                totalLight += SpotLightPBR(
                    light, input.normal, input.worldPos, 
                    cbFrame.cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;
        }
    }
    
	// Gamma correct and return
    return float4(pow(totalLight, 1.0f / 2.2f), 1.0f);
}   
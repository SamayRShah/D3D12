
#include "Lighting.hlsli"

cbuffer ExternalData : register(b0)
{
    uint albedoIndex;
    uint normalMapIndex;
    uint roughnessIndex;
    uint metalnessIndex;
    float2 uvScale;
    float2 uvOffset;
    float3 cameraPosition;
    uint numLights;
    Light lights[MAX_LIGHTS];
}

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
    // get textures from heap
    Texture2D AlbedoTexture = ResourceDescriptorHeap[albedoIndex];
    Texture2D NormalMap = ResourceDescriptorHeap[normalMapIndex];
    Texture2D RoughnessMap = ResourceDescriptorHeap[roughnessIndex];
    Texture2D MetalnessMap = ResourceDescriptorHeap[metalnessIndex];
    
    // cleanup input data
    input.normal = normalize(input.normal);
    input.tangent = normalize(input.tangent);
    
    // adjust uv coords
    input.uv = input.uv * uvScale + uvOffset;
    
    // apply normal map
    input.normal = NormalMapping(NormalMap, BasicSampler, input.uv, input.normal, input.tangent);
    
    // Surface color with gamma correction
    float4 surfaceColor = AlbedoTexture.Sample(BasicSampler, input.uv);
    surfaceColor.rgb = pow(surfaceColor.rgb, 2.2);
    
    // sample roughness / metalness
    float roughness = RoughnessMap.Sample(BasicSampler, input.uv).r;
    float metalness = MetalnessMap.Sample(BasicSampler, input.uv).r;
   
    float3 specColor = lerp(F0_NON_METAL, surfaceColor.rgb, metalness);
    
    // lighting
    float3 totalLight = float3(0, 0, 0);
    
    for (int i = 0; i < numLights; i++)
    {
        Light light = lights[i];
        // Run the correct lighting calculation based on the light's type
        switch (lights[i].Type)
        {
            case LIGHT_TYPE_DIRECTIONAL:
                totalLight += DirLightPBR(
                    light, input.normal, input.worldPos, 
                    cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;
            case LIGHT_TYPE_POINT:
                totalLight += PointLightPBR(
                    light, input.normal, input.worldPos, 
                    cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;

            case LIGHT_TYPE_SPOT:
                totalLight += SpotLightPBR(
                    light, input.normal, input.worldPos, 
                    cameraPosition, roughness, metalness, surfaceColor.rgb, specColor, 0);
                break;
        }
    }
    
	// Gamma correct and return
    return float4(pow(totalLight, 1.0f / 2.2f), 1.0f);
}   
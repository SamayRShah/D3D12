#include "Lighting.hlsli"

cbuffer BindlessData : register(b0)
{
    uint psPerFrameCBIndex;
    uint albedoIndex;
    uint normalIndex;
    uint materialIndex;
    uint depthIndex;
}

struct PSPerFrameData
{
    float4x4 inverseViewProjection;
    float3 cameraPosition;
    int lightCount;
    Light lights[MAX_LIGHTS];
};

struct VertexToPixel
{
    float4 screenPosition : SV_POSITION;
    float2 uv : TEXCOORD;
};

SamplerState BasicSampler : register(s0);


float4 main(VertexToPixel input) : SV_TARGET
{
    ConstantBuffer<PSPerFrameData> cbFrame = ResourceDescriptorHeap[psPerFrameCBIndex];
    
    Texture2D Albedo = ResourceDescriptorHeap[albedoIndex];
    Texture2D Normal = ResourceDescriptorHeap[normalIndex];
    Texture2D Material = ResourceDescriptorHeap[materialIndex];
    Texture2D Depth = ResourceDescriptorHeap[depthIndex];
    
    float4 albedoSample = Albedo.Sample(BasicSampler, input.uv);
    float4 normalSample = Normal.Sample(BasicSampler, input.uv);
    float4 materialSample = Material.Sample(BasicSampler, input.uv);
    float depthSample = Depth.Sample(BasicSampler, input.uv).r;

    float3 normal = normalize(normalSample.xyz * 2.0f - 1.0f);
    float roughness = materialSample.r;
    float metalness = materialSample.g;

    float3 worldPos = ReconstructPosition(input.uv, depthSample, cbFrame.inverseViewProjection);

    float3 specColor = lerp(F0_NON_METAL, albedoSample.rgb, metalness);
    float3 totalLight = float3(0, 0, 0);

    for (int i = 0; i < cbFrame.lightCount; i++)
    {
        Light light = cbFrame.lights[i];
        float3 lightDir = normalize(light.Direction);

        switch (light.Type)
        {
            case LIGHT_TYPE_DIRECTIONAL:
                totalLight += DirLightPBR(
                    light, normal, worldPos,
                    cbFrame.cameraPosition, roughness, metalness, albedoSample.rgb, specColor, 0);
                break;

            case LIGHT_TYPE_POINT:
                totalLight += PointLightPBR(
                    light, normal, worldPos,
                    cbFrame.cameraPosition, roughness, metalness, albedoSample.rgb, specColor, 0);
                break;

            case LIGHT_TYPE_SPOT:
                totalLight += SpotLightPBR(
                    light, normal, worldPos,
                    cbFrame.cameraPosition, roughness, metalness, albedoSample.rgb, specColor, 0);
                break;
        }
    }

    return float4(pow(totalLight, 1.0 / 2.2), 1.0);
}
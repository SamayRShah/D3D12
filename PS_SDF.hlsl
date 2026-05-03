cbuffer BindlessData : register(b0)
{
    uint sdfDataIndex;
}

struct sdfData
{
    matrix inverseViewProjection;
    float3 cameraPosition;
	
    int screenWidth;
    int screenHeight;
	
    float totalTime;
};

struct VertexToPixel
{
    float4 screenPosition : SV_POSITION;
    float2 uv : TEXCOORD;
};

float sdf_sphere(float3 p, float3 center, float radius)
{
    return length(p - center) - radius;
}

float4 main(VertexToPixel input) : SV_TARGET
{
    ConstantBuffer<sdfData> data = ResourceDescriptorHeap[sdfDataIndex];
    
    float4 position = input.screenPosition;
    float3 fragCoord = (position.xyz / position.w) * 0.5 + 0.5; // Normalize to [0,1]
    
    // Convert to world space
    fragCoord = fragCoord * 2.0 - 1.0; // normalize ndc
    
    // Modify sphere's position and radius over time
    float3 dynamicSphereCenter = float3(sin(data.totalTime), cos(data.totalTime), 0.0);
    float dynamicSphereRadius = sin(data.totalTime) * 0.1; // Oscillate the radius

    // SDF value for the moving sphere
    float distance = sdf_sphere(fragCoord, dynamicSphereCenter, dynamicSphereRadius);

    // Return the color based on the SDF distance
    if (distance < 0.0)
        return float4(1.0, 0.0, 0.0, 1.0); // Red inside the sphere
    else
        return float4(0.0, 0.0, 0.0, 1.0); // Black outside the sphere
}
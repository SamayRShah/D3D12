

cbuffer ExternalData : register(b0)
{
    uint vsVertexBufferIndex;
    uint vsCBIndex;
    uint psSkyboxIndex;
}

struct VSPerFrameData
{
    matrix view;
    matrix projection;
};

struct Vertex
{
    float3 localPosition : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct VertexToPixel_Sky
{
    float4 screenPosition : SV_POSITION;
    float3 sampleDir : DIRECTION;
};


// entry
VertexToPixel_Sky main(uint vertexID : SV_VertexID)
{
    // get data
    ConstantBuffer<VSPerFrameData> cb = ResourceDescriptorHeap[vsCBIndex];
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[vsVertexBufferIndex];
    Vertex vert = vb[vertexID];
	
	// setup output
    VertexToPixel_Sky output;

	// remove translation
    matrix viewNoTranslation = cb.view;
    viewNoTranslation._14 = 0;
    viewNoTranslation._24 = 0;
    viewNoTranslation._34 = 0;

	// multiply view projection
    matrix vp = mul(cb.projection, viewNoTranslation);
    output.screenPosition = mul(vp, float4(vert.localPosition, 1.0f));

	// set z = w -- set vertex on far clipping plane
    output.screenPosition.z = output.screenPosition.w;

	// use position as sample direction
    output.sampleDir = vert.localPosition;

    return output;
}
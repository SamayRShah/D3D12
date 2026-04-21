cbuffer BindlessData : register(b0)
{
    uint vsVertexBufferIndex;
    uint vsPerFrameCBIndex;
    uint vsPerObjectCBIndex;
    uint psPerFrameCBIndex;
    uint psPerObjectCBIndex;
}

struct VSPerFrameData
{
    matrix view;
    matrix projection;
};

struct VSPerObjectData
{
    matrix world;
    matrix worldInverseTranspose;
};

// Struct representing a single vertex worth of data
// - This should match the vertex definition in our C++ code
// - By "match", I mean the size, order and number of members
// - The name of the struct itself is unimportant, but should be descriptive
// - Each variable must have a semantic, which defines its usage
struct Vertex
{ 
	float3 localPosition	: POSITION;
	float2 uv				: TEXCOORD;
	float3 normal			: NORMAL;
	float3 tangent			: TANGENT;
};

// Struct representing the data we're sending down the pipeline
// - Should match our pixel shader's input (hence the name: Vertex to Pixel)
// - At a minimum, we need a piece of data defined tagged as SV_POSITION
// - The name of the struct itself is unimportant, but should be descriptive
// - Each variable must have a semantic, which defines its usage
struct VertexToPixel
{
	float4 screenPosition	: SV_POSITION;	// XYZW position (System Value Position)
	float2 uv				: TEXCOORD;
	float3 normal			: NORMAL;
	float3 tangent			: TANGENT;
	float3 worldPos			: POSITION;
};

// --------------------------------------------------------
// The entry point (main method) for our vertex shader
// 
// - Input is id of vertex for bindless vertex fetching
// - Output is a single struct of data to pass down the pipeline
// - Named "main" because that's the default the shader compiler looks for
// --------------------------------------------------------
VertexToPixel main(uint vertexID : SV_VertexID)
{
	// get data
	ConstantBuffer<VSPerFrameData> cbFrame = ResourceDescriptorHeap[vsPerFrameCBIndex];
	ConstantBuffer<VSPerObjectData> cbObject = ResourceDescriptorHeap[vsPerObjectCBIndex];
	StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[vsVertexBufferIndex];
	Vertex vert = vb[vertexID];
	
	// Set up output struct
	VertexToPixel output;
	
	// calc screen position
	matrix wvp = mul(cbFrame.projection, mul(cbFrame.view, cbObject.world));
	output.screenPosition = mul(wvp, float4(vert.localPosition, 1.0f));

	// Make sure the lighting vectors are in world space
	output.normal = normalize(mul((float3x3)cbObject.worldInverseTranspose, vert.normal));
	output.tangent = normalize(mul((float3x3)cbObject.world, vert.tangent));

	// calc vertex world pos
	output.worldPos = mul(cbObject.world, float4(vert.localPosition, 1.0f)).xyz;

	// pass through the uv
	output.uv = vert.uv;

	return output;
}
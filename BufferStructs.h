#pragma once
#include <DirectXMath.h>

#include "Lights.h"

// === Rasterization ===

struct SkyDrawIndices
{
	unsigned int vsVertexBufferIndex;
	unsigned int vsCBIndex;
	unsigned int psSkyboxIndex;
};

struct DrawDescriptorIndices
{
	unsigned int vsVertexBufferIndex;
	unsigned int vsPerFrameCBIndex;
	unsigned int vsPerObjectCBIndex;
	unsigned int psPerFrameCBIndex;
	unsigned int psPerObjectCBIndex;
};

struct VertexShaderPerFrameData
{
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 projection;
};

struct VertexShaderPerObjectData
{
	DirectX::XMFLOAT4X4 world;
	DirectX::XMFLOAT4X4 worldInverseTranspose;
};

struct PixelShaderPerFrameData
{
	DirectX::XMFLOAT3 cameraPosition;
	int lightCount;
	Light lights[MAX_LIGHTS];
};

struct PixelShaderPerObjectData
{
	// constants
	DirectX::XMFLOAT3 color;
	float roughness;
	float metalness;
	float pad[3];

	// textures
	unsigned int albedoIndex;
	unsigned int normalMapIndex;
	unsigned int roughnessIndex;
	unsigned int metalnessIndex;
	DirectX::XMFLOAT2 uvScale;
	DirectX::XMFLOAT2 uvOffset;
};


// === RayTracing ===

// Root constants for bindless resources
struct RayTracingDrawData
{
	unsigned int SceneDataConstantBufferIndex;
	unsigned int EntityDataDescriptorIndex;
	unsigned int SceneTLASDescriptorIndex;
	unsigned int OutputUAVDescriptorIndex;
	unsigned int SkyboxDescriptorIndex;
};

// overall scene data 
struct RayTracingSceneData
{
	DirectX::XMFLOAT4X4 InverseViewProjection;
	DirectX::XMFLOAT3 CameraPosition;
	unsigned int RaysPerPixel;
};

// per entity data
struct RayTracingEntityData
{
	// properties
	DirectX::XMFLOAT4 Color;
	unsigned int VertexBufferDescriptorIndex;
	unsigned int IndexBufferDescriptorIndex;

	// textures
	DirectX::XMFLOAT2 UVScale;
	DirectX::XMFLOAT2 UVOffset;
	unsigned int AlbedoIndex;
	unsigned int NormalMapIndex;
	unsigned int RoughnessIndex;
	unsigned int MetalnessIndex;

	// values
	float Roughness;
	float Metalness;
	float Emissive;
	float IOR;
	float Alpha;
	float pad;
};
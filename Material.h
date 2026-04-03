#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>
#include <unordered_map>
#include <string>

#include "Camera.h"
#include "Transform.h"

class Material
{
public:
	Material(
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState,
		DirectX::XMFLOAT3 tint = DirectX::XMFLOAT3(1,1,1),
		DirectX::XMFLOAT2 uvScale = DirectX::XMFLOAT2(1, 1),
		DirectX::XMFLOAT2 uvOffset = DirectX::XMFLOAT2(0, 0)
	);

	// Getters
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState() { return pipelineState; }
	DirectX::XMFLOAT3 GetColorTint() { return colorTint; }
	DirectX::XMFLOAT2 GetUVScale() { return uvScale; }
	DirectX::XMFLOAT2 GetUVOffset() { return uvOffset; }
	float GetRoughness() { return roughness; }
	float GetMetalness() { return metalness; }
	float GetEmissive() { return emissive; }
	float GetIOR() { return ior; }
	float GetAlpha() { return alpha; }

	// texture getters
	// unsigned int GetTexture(std::string tex) { return textures[tex]; }
	unsigned int GetAlbedoIndex() { return albedoIndex; }
	unsigned int GetNormalMapIndex() { return normalMapIndex; }
	unsigned int GetRoughnessIndex() { return  roughnessIndex; }
	unsigned int GetMetalnessIndex() { return  metalnessIndex; }

	// setters
	void SetPipelineState(Microsoft::WRL::ComPtr <ID3D12PipelineState> ps) { pipelineState = ps; }
	void SetTint(DirectX::XMFLOAT3 t) { colorTint = t; }
	void SetRoughness(float r) { roughness = r; }
	void SetMetalness(float m) { metalness = m; }
	void SetUVScale(DirectX::XMFLOAT2 s) { uvScale = s; }
	void SetUVOffset(DirectX::XMFLOAT2 o) { uvOffset = o; }
	void SetEmissive(float e) { emissive = e; }
	void SetIOR(float i) { ior = i; }
	void SetAlpha(float a) { alpha = a; }

	// texture setters
	// void SetTexture(std::string tex, unsigned int index) { textures[tex] = index; }
	void SetPBR(
		unsigned int a, unsigned int n, unsigned int r, unsigned int m)
	{
		albedoIndex = a; normalMapIndex = n; roughnessIndex = r; metalnessIndex = m;
	}
	void SetAlbedoIndex(unsigned int index) { albedoIndex = index; }
	void SetNormalMapIndex(unsigned int index) { normalMapIndex = index; }
	void SetRoughnessIndex(unsigned int index) { roughnessIndex = index; }
	void SetMetalnessIndex(unsigned int index) { metalnessIndex = index; }

private:
	// pipeline state - replaces vertex / pixel shaders
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	// properties
	DirectX::XMFLOAT3 colorTint;
	DirectX::XMFLOAT2 uvScale;
	DirectX::XMFLOAT2 uvOffset;
	float roughness;
	float metalness;
	float emissive;
	float ior;
	float alpha;

	// Textures
	// std::unordered_map<std::string, unsigned int> textures; // other textures
	unsigned int albedoIndex;
	unsigned int normalMapIndex;
	unsigned int roughnessIndex;
	unsigned int metalnessIndex;
};
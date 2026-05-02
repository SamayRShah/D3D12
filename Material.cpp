#include "Material.h"

Material::Material(
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState,
	DirectX::XMFLOAT3 tint, DirectX::XMFLOAT2 uvScale, DirectX::XMFLOAT2 uvOffset
) 
	: 
	pipelineState(pipelineState), colorTint(tint), uvScale(uvScale), uvOffset(uvOffset),
	roughness(1), metalness(1), emissive(0), ior(1.5f), alpha(1)
{}
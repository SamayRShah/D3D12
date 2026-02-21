#include "Material.h"

Material::Material(
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState,
	DirectX::XMFLOAT3 tint, DirectX::XMFLOAT2 uvScale, DirectX::XMFLOAT2 uvOffset
) 
	: 
	pipelineState(pipelineState), colorTint(tint), uvScale(uvScale), uvOffset(uvOffset),
	albedoIndex(-1), normalMapIndex(-1), roughnessIndex(-1), metalnessIndex(-1)
{}
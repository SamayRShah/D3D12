#pragma once

#include <memory>

#include "Mesh.h"
#include "Camera.h"

class SkyBox
{
public:
	SkyBox(
		const wchar_t* right, const wchar_t* left,
		const wchar_t* up, const wchar_t* down,
		const wchar_t* front, const wchar_t* back,
		std::shared_ptr<Mesh> mesh);
	
	void Draw(std::shared_ptr<Camera> camera);
	unsigned int GetDescriptorIndex() { return skyBoxDescriptorIndex; }
private:
	// init helpers
	void InitRenderStates();

	// d3d12 objects
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	unsigned int skyBoxDescriptorIndex;
	std::shared_ptr<Mesh> skyMesh;
};
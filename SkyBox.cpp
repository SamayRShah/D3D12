#include "SkyBox.h"
#include "Graphics.h"
#include "BufferStructs.h"
#include "PathHelpers.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

SkyBox::SkyBox
(
	const wchar_t* right, const wchar_t* left,
	const wchar_t* up, const wchar_t* down,
	const wchar_t* front, const wchar_t* back,
	std::shared_ptr<Mesh> mesh
) : skyMesh(mesh)
{
	InitRenderStates();
	texture = Graphics::CreateCubeMap(right, left, up, down, front, back);
}

void SkyBox::InitRenderStates()
{
	// root sig
	{
		// root params
		D3D12_ROOT_PARAMETER rootParams[1] = {};
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[0].Constants.Num32BitValues = sizeof(SkyDrawIndices) / sizeof(unsigned int);
		rootParams[0].Constants.RegisterSpace = 0;
		rootParams[0].Constants.ShaderRegister = 0;

		// basic sampler
		D3D12_STATIC_SAMPLER_DESC anisoWrap = {};
		anisoWrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.Filter = D3D12_FILTER_ANISOTROPIC;
		anisoWrap.MaxAnisotropy = 16;
		anisoWrap.MaxLOD = D3D12_FLOAT32_MAX;
		anisoWrap.ShaderRegister = 0;  // register(s0)
		anisoWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC samplers[] = { anisoWrap };

		// serialize root sig
		D3D12_ROOT_SIGNATURE_DESC rootSig = {};
		rootSig.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		rootSig.NumParameters = ARRAYSIZE(rootParams);
		rootSig.pParameters = rootParams;
		rootSig.NumStaticSamplers = ARRAYSIZE(samplers);
		rootSig.pStaticSamplers = samplers;

		ID3DBlob* serializedRootSig = 0;
		ID3DBlob* errors = 0;

		D3D12SerializeRootSignature(
			&rootSig,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&serializedRootSig,
			&errors);

		// error check
		if (errors != 0)
		{
			OutputDebugString((wchar_t*)errors->GetBufferPointer());
		}

		Graphics::Device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(rootSignature.GetAddressOf()));
	}

	// pipeline state
	{
		// load shaders
		Microsoft::WRL::ComPtr<ID3DBlob> vsByteCode;
		Microsoft::WRL::ComPtr<ID3DBlob> psByteCode;
		D3DReadFileToBlob(FixPath(L"VS_SkyBox.cso").c_str(), vsByteCode.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_SkyBox.cso").c_str(), psByteCode.GetAddressOf());

		// desc pipeline state
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// input assembler
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// root sig
		psoDesc.pRootSignature = rootSignature.Get();

		// shaders
		psoDesc.VS.pShaderBytecode = vsByteCode->GetBufferPointer();
		psoDesc.VS.BytecodeLength = vsByteCode->GetBufferSize();
		psoDesc.PS.pShaderBytecode = psByteCode->GetBufferPointer();
		psoDesc.PS.BytecodeLength = psByteCode->GetBufferSize();

		// rts
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		// states
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; // show inner faces
		psoDesc.RasterizerState.DepthClipEnable = true;

		psoDesc.DepthStencilState.DepthEnable = true;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		psoDesc.SampleMask = 0xffffffff;
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
	}
}

void SkyBox::Draw(std::shared_ptr<Camera> camera)
{
	// set pipeline state
	Graphics::CommandList->SetPipelineState(pipelineState.Get());
	Graphics::CommandList->SetGraphicsRootSignature(rootSignature.Get());

	// draw data
	SkyDrawIndices drawData{};
	drawData.psSkyboxIndex = texture.SRV.GPUDescriptorIndex;
	drawData.vsVertexBufferIndex = Graphics::GetDescriptorIndex(skyMesh->GetVertexBufferDescriptorHandle());

	// per frame data
	{
		VertexShaderPerFrameData vsFrame{};
		vsFrame.view = camera->GetView();
		vsFrame.projection = camera->GetProjection();

		D3D12_GPU_DESCRIPTOR_HANDLE cbHandleVS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
			(void*)(&vsFrame), sizeof(VertexShaderPerFrameData));

		drawData.vsCBIndex = Graphics::GetDescriptorIndex(cbHandleVS);
	}

	//copy data
	Graphics::CommandList->SetGraphicsRoot32BitConstants(
		0,
		sizeof(SkyDrawIndices) / sizeof(unsigned int),
		&drawData,
		0);

	D3D12_INDEX_BUFFER_VIEW ibv = skyMesh->GetIndexBufferView();
	Graphics::CommandList->IASetIndexBuffer(&ibv);
	Graphics::CommandList->DrawIndexedInstanced((UINT)skyMesh->GetIndexCount(), 1, 0, 0, 0);
}
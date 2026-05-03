#include <DirectXMath.h>
#include <d3dcompiler.h>

#include "Game.h"
#include "Mesh.h"
#include "GameEntity.h"
#include "Graphics.h"
#include "Vertex.h"
#include "BufferStructs.h"
#include "Input.h"
#include "PathHelpers.h"
#include "Window.h"
#include "RayTracing.h"
#include "Utils.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_win32.h"

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")

// For the DirectX Math library
using namespace DirectX;

#define ROOT_PARAM_INPUTS 8

// random range helper macro
#define RandomRange(min, max) (float)rand() / RAND_MAX * (max - min) + min

// temporary asset macros
#define ASSET(asset) FixPath(AssetPath + asset).c_str()
#define LOAD_PBR_ASSET(asset) \
	Graphics::LoadTexture(ASSET(L"Textures/PBR/" + asset + L"_albedo.png")), \
	Graphics::LoadTexture(ASSET(L"Textures/PBR/" + asset + L"_normals.png")), \
	Graphics::LoadTexture(ASSET(L"Textures/PBR/" + asset + L"_roughness.png")), \
	Graphics::LoadTexture(ASSET(L"Textures/PBR/" + asset + L"_metal.png"))

#define SKY_ASSET(path) \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/right.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/left.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/up.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/down.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/front.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/back.png").c_str()  

// --------------------------------------------------------
// The constructor is called after the window and graphics API
// are initialized but before the game loop begins
// --------------------------------------------------------
Game::Game()
{
	// Reserve a descriptor slot for ImGui's font texture
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
	Graphics::ReserveDescriptorHeapSlot(&cpuHandle, &gpuHandle);

	// Initialize ImGui itself & platform/renderer backends
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(Window::Handle());
	{
		ImGui_ImplDX12_InitInfo info{};
		info.CommandQueue = Graphics::CommandQueue.Get();
		info.Device = Graphics::Device.Get();
		info.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		info.LegacySingleSrvCpuDescriptor = cpuHandle;
		info.LegacySingleSrvGpuDescriptor = gpuHandle;
		info.NumFramesInFlight = Graphics::NumBackBuffers;
		info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		info.SrvDescriptorHeap = Graphics::CBVSRVDescriptorHeap.Get();

		ImGui_ImplDX12_Init(&info);
	}

	// seed random
	srand((unsigned int)time(0));

	// init ray tracing
	RayTracing::Initialize(
		Window::Width(), Window::Height(),
		FixPath(L"RayTracing.cso")
	);

	CreateRootSigAndPipelineState();
	CreateEntities();
	CreateLights();

	// create camera
	camera = std::make_shared<FPSCamera>(
		XMFLOAT3(0, 0, -10),	// pos
		5.0f,					// move speed
		0.002f,					// look speed
		XM_PIDIV4,				// fov
		Window::AspectRatio(),  // Aspect ratio
		0.01f,					// near clip
		100.0f,					// far clip
		CameraProjectionType::Perspective
	);
}


// --------------------------------------------------------
// Clean up memory or objects created by this class
// 
// Note: Using smart pointers means there probably won't
//       be much to manually clean up here!
// --------------------------------------------------------
Game::~Game()
{
	// wait for GPU before shutdown
	Graphics::WaitForGPU();

	// ImGui clean up
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Game::CreateRootSigAndPipelineState()
{
	// load shaders
	Microsoft::WRL::ComPtr<ID3DBlob> vsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> psByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> fullscreenVS;
	Microsoft::WRL::ComPtr<ID3DBlob> rasterPS;
	Microsoft::WRL::ComPtr<ID3DBlob> compositePS;
	Microsoft::WRL::ComPtr<ID3DBlob> lightingPS;
	Microsoft::WRL::ComPtr<ID3DBlob> sdfPS;

	// load shaders
	{
		D3DReadFileToBlob(FixPath(L"VS_PBR.cso").c_str(), vsByteCode.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_PBR.cso").c_str(), psByteCode.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"VS_FullScreen.cso").c_str(), fullscreenVS.GetAddressOf());

		D3DReadFileToBlob(FixPath(L"PS_Raster.cso").c_str(), rasterPS.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_Composite.cso").c_str(), compositePS.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_Lighting.cso").c_str(), lightingPS.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_SDF.cso").c_str(), sdfPS.GetAddressOf());
	}

	// root signature
	{
		D3D12_ROOT_PARAMETER rootParams[1] = {};

		// root params
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[0].Constants.Num32BitValues = ROOT_PARAM_INPUTS;
		rootParams[0].Constants.RegisterSpace = 0;
		rootParams[0].Constants.ShaderRegister = 0;

		// basic shared sampler
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

		// create the root sig
		Graphics::Device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(rootSignature.GetAddressOf()));
	}

	// Pipeline state
	{
		// Describe the pipeline state
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// -- Input assembler related ---
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		// Overall primitive topology type (triangle, line, etc.) is set here 
		// IASetPrimTop() is still used to set list/strip/adj options
		// See: https://docs.microsoft.com/en-us/windows/desktop/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

		// Root sig
		psoDesc.pRootSignature = rootSignature.Get();

		// -- Shaders (VS/PS) --- 
		psoDesc.VS.pShaderBytecode = vsByteCode->GetBufferPointer();
		psoDesc.VS.BytecodeLength = vsByteCode->GetBufferSize();
		psoDesc.PS.pShaderBytecode = rasterPS->GetBufferPointer();
		psoDesc.PS.BytecodeLength = rasterPS->GetBufferSize();

		// -- Render targets ---
		psoDesc.NumRenderTargets = 4;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_R32_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		// -- States ---
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.DepthClipEnable = true;

		psoDesc.DepthStencilState.DepthEnable = true;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		// -- Misc ---
		psoDesc.SampleMask = 0xffffffff;

		// Create the pipe state object
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));

		// ============================================
		// Also create the "full screen texture" PSO
		// - Assuming the same root sig is compatible

		// Disable depth testing for full-screen passes so they don't fail against the scene depth
		psoDesc.DepthStencilState.DepthEnable = false;

		psoDesc.PS.BytecodeLength = compositePS->GetBufferSize();
		psoDesc.PS.pShaderBytecode = compositePS->GetBufferPointer();

		psoDesc.VS.BytecodeLength = fullscreenVS->GetBufferSize();
		psoDesc.VS.pShaderBytecode = fullscreenVS->GetBufferPointer();

		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN; // Means "not used" here
		psoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(fullScreenTexturePSO.GetAddressOf()));

		psoDesc.PS.BytecodeLength = lightingPS->GetBufferSize();
		psoDesc.PS.pShaderBytecode = lightingPS->GetBufferPointer();

		psoDesc.VS.BytecodeLength = fullscreenVS->GetBufferSize();
		psoDesc.VS.pShaderBytecode = fullscreenVS->GetBufferPointer();

		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN; // Means "not used" here
		psoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(lightingPSO.GetAddressOf()));

		psoDesc.PS.BytecodeLength = sdfPS->GetBufferSize();
		psoDesc.PS.pShaderBytecode = sdfPS->GetBufferPointer();

		psoDesc.VS.BytecodeLength = fullscreenVS->GetBufferSize();
		psoDesc.VS.pShaderBytecode = fullscreenVS->GetBufferPointer();

		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN; // Means "not used" here
		psoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;

		// Create the SDF PSO
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(sdfPSO.GetAddressOf()));
	}

	// Set up the viewport and scissor rectangle
	{
		// Set up the viewport so we render into the correct
		// portion of the render target
		viewport = {};
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = (float)Window::Width();
		viewport.Height = (float)Window::Height();
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		// Define a scissor rectangle that defines a portion of
		// the render target for clipping.  This is different from
		// a viewport in that it is applied after the pixel shader.
		// We need at least one of these, but we're rendering to 
		// the entire window, so it'll be the same size.
		scissorRect = {};
		scissorRect.left = 0;
		scissorRect.top = 0;
		scissorRect.right = Window::Width();
		scissorRect.bottom = Window::Height();
	}

	// Create RTV heap for render targets
	D3D12_DESCRIPTOR_HEAP_DESC dhDesc{};
	dhDesc.NumDescriptors = MaxRenderTargets;
	dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	Graphics::Device->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(rtvDescriptorHeap.GetAddressOf()));

	D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_start = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	unsigned int descSize = Graphics::Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Create render targets (note that depth uses R32 format!)
	RenderTargets[GBUFFER_ALBEDO] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	RenderTargets[GBUFFER_NORMALS] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	RenderTargets[GBUFFER_MATERIAL] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	RenderTargets[GBUFFER_DEPTH] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, DXGI_FORMAT_R32_FLOAT, 1, 0, 0, 0);
	RenderTargets[LIGHT_BUFFER] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	RenderTargets[SDF] = Graphics::CreateTexture(Window::Width(), Window::Height(), 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	// Update the texture details to point to contiguous spots in the RTV heap
	for (unsigned int i = 0; i < 6; i++)
	{
		RenderTargets[i].RTV = rtv_cpu_start;
		RenderTargets[i].RTV.ptr += descSize * i;
	}

	// Create RTVs for render targets
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Create the RTVs next to each other in the heap
	Graphics::Device->CreateRenderTargetView(RenderTargets[GBUFFER_ALBEDO].Texture.Get(), &rtvDesc, RenderTargets[GBUFFER_ALBEDO].RTV);
	Graphics::Device->CreateRenderTargetView(RenderTargets[GBUFFER_NORMALS].Texture.Get(), &rtvDesc, RenderTargets[GBUFFER_NORMALS].RTV);
	Graphics::Device->CreateRenderTargetView(RenderTargets[GBUFFER_MATERIAL].Texture.Get(), &rtvDesc, RenderTargets[GBUFFER_MATERIAL].RTV);
	Graphics::Device->CreateRenderTargetView(RenderTargets[LIGHT_BUFFER].Texture.Get(), &rtvDesc, RenderTargets[LIGHT_BUFFER].RTV);
	Graphics::Device->CreateRenderTargetView(RenderTargets[SDF].Texture.Get(), &rtvDesc, RenderTargets[SDF].RTV);

	// Depth needs different format
	rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	Graphics::Device->CreateRenderTargetView(RenderTargets[GBUFFER_DEPTH].Texture.Get(), &rtvDesc, RenderTargets[GBUFFER_DEPTH].RTV);

	// Create SRVs, too
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Reserve 4 SRVs
	for (unsigned int i = 0; i < 6; i++)
		Graphics::ReserveDescriptorHeapSlot(&RenderTargets[i].SRV.CPUHandle, &RenderTargets[i].SRV.GPUHandle);

	// Create
	Graphics::Device->CreateShaderResourceView(RenderTargets[GBUFFER_ALBEDO].Texture.Get(), &srv, RenderTargets[GBUFFER_ALBEDO].SRV.CPUHandle);
	Graphics::Device->CreateShaderResourceView(RenderTargets[GBUFFER_NORMALS].Texture.Get(), &srv, RenderTargets[GBUFFER_NORMALS].SRV.CPUHandle);
	Graphics::Device->CreateShaderResourceView(RenderTargets[GBUFFER_MATERIAL].Texture.Get(), &srv, RenderTargets[GBUFFER_MATERIAL].SRV.CPUHandle);
	Graphics::Device->CreateShaderResourceView(RenderTargets[LIGHT_BUFFER].Texture.Get(), &srv, RenderTargets[LIGHT_BUFFER].SRV.CPUHandle);
	Graphics::Device->CreateShaderResourceView(RenderTargets[SDF].Texture.Get(), &srv, RenderTargets[SDF].SRV.CPUHandle);

	// Depth has different format
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	Graphics::Device->CreateShaderResourceView(RenderTargets[GBUFFER_DEPTH].Texture.Get(), &srv, RenderTargets[GBUFFER_DEPTH].SRV.CPUHandle);

	// Update SRV indices
	for (unsigned int i = 0; i < 6; i++)
		RenderTargets[i].SRV.GPUDescriptorIndex = Graphics::GetDescriptorIndex(RenderTargets[i].SRV.GPUHandle);
}


// --------------------------------------------------------
// Creates the geometry we're going to draw
// --------------------------------------------------------
void Game::CreateEntities()
{
	// load meshes
	auto mandoMesh = std::make_shared<Mesh>("Mando", ASSET(L"Meshes/Mando.obj"));
	auto cubeMesh = std::make_shared<Mesh>("Cube", ASSET(L"Meshes/cube.obj"));

	std::vector<std::shared_ptr<Mesh>> meshes;
	auto torusMesh = std::make_shared<Mesh>("Torus", ASSET(L"Meshes/torus.obj"));
	meshes.push_back(torusMesh);
	auto sphereMesh = std::make_shared<Mesh>("Sphere", ASSET(L"Meshes/sphere.obj"));
	meshes.push_back(sphereMesh);
	auto crateMesh = std::make_shared<Mesh>("Crate", ASSET(L"Meshes/crate_wood.obj"));
	meshes.push_back(crateMesh);
	auto helixMesh = std::make_shared<Mesh>("Helix", ASSET(L"Meshes/helix.obj"));
	meshes.push_back(helixMesh);

	// create skyBox
	skyBox = std::make_shared<SkyBox>(SKY_ASSET(L"Clouds Pink"), cubeMesh);

	// create materials
	auto floorMat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.2f, 0.15f));
	floorMat->SetTint(XMFLOAT3(0.6f, 0.6f, 0.9f));
	floorMat->SetRoughness(0.2f);
	floorMat->SetMetalness(0);

	auto mandoMat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.6f, 0.5f));
	mandoMat->SetAlbedoTexture(Graphics::LoadTexture(ASSET(L"Textures/mando.png")));
	mandoMat->SetNormalMapTexture(Graphics::LoadTexture(ASSET(L"Textures/mando_normals.png")));
	mandoMat->SetMetalness(0.8f);
	mandoMat->SetRoughness(0.2f);

	auto bronzeMat = std::make_shared<Material>(pipelineState);
	bronzeMat->SetPBR(LOAD_PBR_ASSET(L"bronze"));

	auto crateMat = std::make_shared<Material>(pipelineState);
	crateMat->SetPBR(LOAD_PBR_ASSET(L"crate_wood"));

	// Floor
	auto floor = std::make_shared<GameEntity>(cubeMesh, floorMat);
	floor->GetTransform()->SetScale(50);
	floor->GetTransform()->SetPosition(0, -51, 0);
	entities.push_back(floor);

	// Spinning mando
	auto mando = std::make_shared<GameEntity>(mandoMesh, mandoMat);
	mando->GetTransform()->SetPosition(0, 3, 0);
	entities.push_back(mando);

	for (int i = 0; i < 8; i++)
	{
		auto mat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.8f, 0.4f));
		mat->SetRoughness(RandomRange(0.0f, 1.0f) * RandomRange(0.0f, 1.0f));
		mat->SetMetalness(RandomRange(0.0f, 1.0f) * RandomRange(0.0f, 1.0f));
		float scale = RandomRange(0.25f, 0.6f);

		auto newEnt = std::make_shared<GameEntity>(meshes[(size_t)(RandomRange(0, meshes.size()))], mat);
		newEnt->GetTransform()->SetScale(scale);
		newEnt->GetTransform()->SetPosition(
			RandomRange(-9, 9),
			-1 + scale,
			RandomRange(-9, 9));

		entities.push_back(newEnt);
	}
	for (int i = 0; i < 8; i++)
	{
		float scale = RandomRange(0.25f, 0.6f);
		auto newEnt = std::make_shared<GameEntity>(meshes[(size_t)(RandomRange(0, meshes.size()))], bronzeMat);
		newEnt->GetTransform()->SetScale(scale);
		newEnt->GetTransform()->SetPosition(
			RandomRange(-9, 9),
			-1 + scale,
			RandomRange(-9, 9));

		entities.push_back(newEnt);
	}
	for (int i = 0; i < 8; i++)
	{
		float scale = RandomRange(0.25f, 0.6f);
		auto newEnt = std::make_shared<GameEntity>(meshes[(size_t)(RandomRange(0, meshes.size()))], crateMat);
		newEnt->GetTransform()->SetScale(scale);
		newEnt->GetTransform()->SetPosition(
			RandomRange(-9, 9),
			-1 + scale,
			RandomRange(-9, 9));

		entities.push_back(newEnt);
	}

	// Once we have all of the BLASs ready, we can make a TLAS
	RayTracing::CreateEntityDataBuffer(entities);
	RayTracing::CreateTopLevelAccelerationStructureForScene(entities);

	// finalize initialization and wait for GPU
	Graphics::CloseAndExecuteCommandList();
	Graphics::WaitForGPU();
	Graphics::ResetAllocatorAndCommandList(0);
}

void Game::CreateLights()
{
	// reset lights
	lights.clear();

	// setup directional lights
	Light dir1 = {};
	dir1.Type = LIGHT_TYPE_DIRECTIONAL;
	dir1.Direction = XMFLOAT3(-1, -1, 0);
	dir1.Color = XMFLOAT3(1, 0, 0);
	dir1.Intensity = 0.5f;

	Light dir2 = {};
	dir2.Type = LIGHT_TYPE_DIRECTIONAL;
	dir2.Direction = XMFLOAT3(0, -1, 0);
	dir2.Color = XMFLOAT3(0, 1, 0);
	dir2.Intensity = 0.2f;

	Light dir3 = {};
	dir3.Type = LIGHT_TYPE_DIRECTIONAL;
	dir3.Direction = XMFLOAT3(0, -1, -1);
	dir3.Color = XMFLOAT3(0, 0, 1);
	dir3.Intensity = 0.6f;

	lights.push_back(dir1);
	lights.push_back(dir2);
	lights.push_back(dir3);

	lightCount = 3;

	// create rest of lights
	while (lights.size() < MAX_LIGHTS)
	{
		Light point = {};
		point.Type = LIGHT_TYPE_POINT;
		point.Position = XMFLOAT3(RandomRange(-25, 25), RandomRange(0.1f, 5), RandomRange(-25, 25));
		point.Color = Utils::HSLColor(-1, 0.8f, 0.4f);
		point.Range = RandomRange(5.0f, 10.0f);
		point.Intensity = RandomRange(0.1f, 5.0f);

		lights.push_back(point);
		lightCount++;
	}

	// resize to exact size
	lights.resize(MAX_LIGHTS);
}

// --------------------------------------------------------
// Handle resizing to match the new window size
//  - Eventually, we'll want to update our 3D camera
// --------------------------------------------------------
void Game::OnResize()
{
	// setup viewport to render into correct portion of rt
	viewport = {};
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float)Window::Width();
	viewport.Height = (float)Window::Height();
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// defines portion of rt for clipping. 
	// different from viewport since is applied after shaders
	// at least one required, but since rendering entire window,
	// it is same size
	scissorRect = {};
	scissorRect.left = 0;
	scissorRect.right = 0;
	scissorRect.right = Window::Width();
	scissorRect.bottom = Window::Height();

	if (camera) camera->UpdateProjectionMatrix(Window::AspectRatio());
	RayTracing::ResizeOutputUAV(Window::Width(), Window::Height());
}


// --------------------------------------------------------
// Update your game here - user input, move objects, AI, etc.
// --------------------------------------------------------
void Game::Update(float deltaTime, float totalTime)
{
	// Set up the new frame for ImGui, then build this frame's UI
	UINewFrame(deltaTime);
	BuildUI();

	// Example input checking: Quit if the escape key is pressed
	if (Input::KeyDown(VK_ESCAPE))
		Window::Quit();

	camera->Update(deltaTime);

	entities[1]->GetTransform()->Rotate(0, deltaTime * 0.5f, 0);
	entities[1]->GetTransform()->SetPosition(XMFLOAT3(0, sinf(totalTime) * 0.5f + 2.5f, 0));
	// Move stuff
	for (int i = 2; i < entities.size(); i++)
	{
		XMFLOAT3 pos = entities[i]->GetTransform()->GetPosition();
		switch (i % 2)
		{
		case 0:
			pos.x = sin((totalTime + i) * 0.4f) * 4;
			pos.y = sinf((totalTime + i) * 0.8f) + 1;
			break;

		case 1:
			pos.z = sin((totalTime + i) * 0.4f) * 4;
			pos.y = cosf((totalTime + i) * 0.8f) + 1;
			break;
		}
		entities[i]->GetTransform()->SetPosition(pos);
		entities[i]->GetTransform()->Rotate(deltaTime * 0.5f, deltaTime * 0.5f, deltaTime * 0.5f);
	}
}


void Game::Draw(float deltaTime, float totalTime)
{
	auto cmd = Graphics::CommandList.Get();
	auto backBuffer = Graphics::BackBuffers[Graphics::SwapChainIndex()];

	// Begin frame
	{
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Transition.pResource = backBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &rb);

		float clearColor[] = { 0,0,0,1 };

		cmd->ClearRenderTargetView(
			Graphics::RTVHandles[Graphics::SwapChainIndex()],
			clearColor, 0, 0);

		cmd->ClearDepthStencilView(
			Graphics::DSVHandle,
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f, 0, 0, 0);
	}

	// rasterize to gbuffer
	{
		float clearColor[] = { 0,0,0,1 };
		float depthClear[] = { 1,0,0,0 };

		// Transition + clear GBuffer
		for (unsigned int i = 0; i < 4; i++)
		{
			D3D12_RESOURCE_BARRIER rb = {};
			rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			rb.Transition.pResource = RenderTargets[i].Texture.Get();
			rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			cmd->ResourceBarrier(1, &rb);

			cmd->ClearRenderTargetView(
				RenderTargets[i].RTV,
				i == GBUFFER_DEPTH ? depthClear : clearColor,
				0, 0);
		}

		cmd->SetPipelineState(pipelineState.Get());
		cmd->SetDescriptorHeaps(1, Graphics::CBVSRVDescriptorHeap.GetAddressOf());
		cmd->SetGraphicsRootSignature(rootSignature.Get());

		cmd->OMSetRenderTargets(
			4,
			&RenderTargets[GBUFFER_ALBEDO].RTV,
			true,
			&Graphics::DSVHandle);

		cmd->RSSetViewports(1, &viewport);
		cmd->RSSetScissorRects(1, &scissorRect);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		DrawDescriptorIndices drawData{};

		// ---- Per-frame data ----
		{
			VertexShaderPerFrameData vsFrame{};
			vsFrame.view = camera->GetView();
			vsFrame.projection = camera->GetProjection();

			auto cb = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&vsFrame, sizeof(vsFrame));
			drawData.vsPerFrameCBIndex = Graphics::GetDescriptorIndex(cb);
		}

		// ---- Draw entities ----
		for (auto& e : entities)
		{
			auto mat = e->GetMaterial();
			cmd->SetPipelineState(mat->GetPipelineState().Get());

			drawData.vsVertexBufferIndex =
				Graphics::GetDescriptorIndex(e->GetMesh()->GetVertexBufferDescriptorHandle());

			// VS per-object
			{
				VertexShaderPerObjectData vs{};
				vs.world = e->GetTransform()->GetWorldMatrix();
				vs.worldInverseTranspose = e->GetTransform()->GetWorldInverseTransposeMatrix();

				auto cb = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&vs, sizeof(vs));
				drawData.vsPerObjectCBIndex = Graphics::GetDescriptorIndex(cb);
			}

			// PS per-object
			{
				PixelShaderPerObjectData ps{};
				auto m = mat;
				ps.albedoIndex = m->GetAlbedoIndex();
				ps.normalMapIndex = m->GetNormalMapIndex();
				ps.roughnessIndex = m->GetRoughnessIndex();
				ps.metalnessIndex = m->GetMetalnessIndex();
				ps.color = m->GetColorTint();
				ps.metalness = m->GetMetalness();
				ps.roughness = m->GetRoughness();
				ps.uvScale = m->GetUVScale();
				ps.uvOffset = m->GetUVOffset();

				auto cb = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&ps, sizeof(ps));
				drawData.psPerObjectCBIndex = Graphics::GetDescriptorIndex(cb);
			}

			cmd->SetGraphicsRoot32BitConstants(
				0,
				sizeof(DrawDescriptorIndices) / sizeof(unsigned int),
				&drawData,
				0);

			auto mesh = e->GetMesh();
			auto ibv = mesh->GetIndexBufferView();
			cmd->IASetIndexBuffer(&ibv);

			cmd->DrawIndexedInstanced((UINT)mesh->GetIndexCount(), 1, 0, 0, 0);
		}

		// Skybox still belongs to raster pass
		skyBox->Draw(camera);

		// Transition GBuffer -> SRV for ray tracing usage
		for (unsigned int i = 0; i < 4; i++)
		{
			D3D12_RESOURCE_BARRIER rb = {};
			rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			rb.Transition.pResource = RenderTargets[i].Texture.Get();
			rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			cmd->ResourceBarrier(1, &rb);
		}
	}

	//// ray trace - didn't end up working
	//{
	//	RayTracing::CreateTopLevelAccelerationStructureForScene(entities);
	//	RayTracing::Raytrace(camera, skyBox->GetDescriptorIndex());
	//}

	// lighting
	{
		// Transition lighting buffer (or reuse swapchain buffer temporarily) to render target
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Transition.pResource = RenderTargets[LIGHT_BUFFER].Texture.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &rb);
	
		// Clear lighting buffer
		float clearColor[] = { 0,0,0,1 };
		cmd->ClearRenderTargetView(RenderTargets[LIGHT_BUFFER].RTV, clearColor, 0, 0);
	
		// Set render target to lighting buffer
		cmd->OMSetRenderTargets(
			1,
			&RenderTargets[LIGHT_BUFFER].RTV,
			true,
			&Graphics::DSVHandle);
	
		cmd->SetPipelineState(lightingPSO.Get());
		cmd->SetGraphicsRootSignature(rootSignature.Get());
	
		// Bind GBuffer SRVs
		cmd->SetDescriptorHeaps(1, Graphics::CBVSRVDescriptorHeap.GetAddressOf());
	
		LightingIndices data{};
		data.GBufferAlbedoIndex = RenderTargets[GBUFFER_ALBEDO].SRV.GPUDescriptorIndex;
		data.GBufferDepthIndex = RenderTargets[GBUFFER_DEPTH].SRV.GPUDescriptorIndex;
		data.GBufferMaterialIndex = RenderTargets[GBUFFER_MATERIAL].SRV.GPUDescriptorIndex;
		data.GBufferNormalIndex = RenderTargets[GBUFFER_NORMALS].SRV.GPUDescriptorIndex;
	
		PixelShaderPerFrameData psFrame{};
		psFrame.inverseViewProjection = camera->GetInverseViewProjection();
		psFrame.cameraPosition = camera->GetTransform()->GetPosition();
		psFrame.lightCount = lightCount;
		memcpy(psFrame.lights, &lights[0], sizeof(Light)* lightCount);
	
		auto cb = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&psFrame, sizeof(psFrame));
		data.psPerFrameCBIndex = Graphics::GetDescriptorIndex(cb);
	
	
		cmd->SetGraphicsRoot32BitConstants(
			0,
			sizeof(LightingIndices) / 4,
			&data,
			0);
	
		// Draw fullscreen triangle to compute lighting
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->DrawInstanced(3, 1, 0, 0);
	
		// Transition lighting buffer to SRV for composite pass
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Transition.pResource = RenderTargets[LIGHT_BUFFER].Texture.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &rb);
	}

	// SDF pass
	// {
	// 	// Transition SDF texture to render target
	// 	D3D12_RESOURCE_BARRIER rb = {};
	// 	rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	// 	rb.Transition.pResource = RenderTargets[SDF].Texture.Get(); 
	// 	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	// 	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// 	rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	// 	cmd->ResourceBarrier(1, &rb);
	// 
	// 	// Clear SDF texture
	// 	float clearColor[4] = { 0, 0, 0, 0 }; 
	// 	cmd->ClearRenderTargetView(RenderTargets[SDF].RTV, clearColor, 0, 0);
	// 
	// 	// Set pipeline state to SDF PSO
	// 	cmd->SetPipelineState(sdfPSO.Get());
	// 	cmd->SetGraphicsRootSignature(rootSignature.Get());
	// 
	// 	// Set descriptor heaps
	// 	cmd->SetDescriptorHeaps(1, Graphics::CBVSRVDescriptorHeap.GetAddressOf());
	// 
	// 	// Create and fill the SDF data structure
	// 	SDFPassData sdfData{};
	// 	sdfData.inverseViewProjection = camera->GetInverseViewProjection();
	// 	sdfData.cameraPosition = camera->GetTransform()->GetPosition();
	// 	sdfData.screenWidth = Window::Width();
	// 	sdfData.screenHeight = Window::Height();
	// 	sdfData.totalTime = totalTime;
	// 	
	// 	// Create a constant buffer and fill it with SDF data
	// 	auto cb = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&sdfData, sizeof(sdfData));
	// 
	// 	cmd->SetGraphicsRoot32BitConstants(
	// 		0, 1,
	// 		&sdfData, 0);
	// 
	// 	// set rt to sdf
	// 	cmd->OMSetRenderTargets(1, &RenderTargets[SDF].RTV, true, &Graphics::DSVHandle);
	// 
	// 	// fullscreen quad
	// 	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// 	cmd->DrawInstanced(3, 1, 0, 0);
	// 
	// 	// Transition SDF texture to SRV for later passes
	// 	rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	// 	rb.Transition.pResource = RenderTargets[SDF].Texture.Get();
	// 	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// 	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	// 	rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	// 	cmd->ResourceBarrier(1, &rb);
	// }

	// composite pass
	{
		// After lighting pass
		cmd->SetGraphicsRootSignature(rootSignature.Get());
		cmd->SetPipelineState(fullScreenTexturePSO.Get());

		cmd->OMSetRenderTargets(
			1,
			&Graphics::RTVHandles[Graphics::SwapChainIndex()],
			true,
			&Graphics::DSVHandle);

		CompositeIndices data{};
		data.GBufferAlbedoIndex = RenderTargets[GBUFFER_ALBEDO].SRV.GPUDescriptorIndex;
		data.GBufferDepthIndex = RenderTargets[GBUFFER_DEPTH].SRV.GPUDescriptorIndex;
		data.GBufferMaterialIndex = RenderTargets[GBUFFER_MATERIAL].SRV.GPUDescriptorIndex;
		data.GBufferNormalIndex = RenderTargets[GBUFFER_NORMALS].SRV.GPUDescriptorIndex;
		data.LightingIndex = RenderTargets[LIGHT_BUFFER].SRV.GPUDescriptorIndex;
		data.SDFIndex = RenderTargets[SDF].SRV.GPUDescriptorIndex;

		cmd->SetGraphicsRoot32BitConstants(
			0,
			sizeof(CompositeIndices) / 4,
			&data,
			0);

		cmd->DrawInstanced(3, 1, 0, 0);
	}

	// UI
	{
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	// Present
	{
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		rb.Transition.pResource = backBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &rb);

		Graphics::CloseAndExecuteCommandList();

		bool vsync = Graphics::VsyncState();
		Graphics::SwapChain->Present(
			vsync ? 1 : 0,
			vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);

		Graphics::AdvanceSwapChainIndex();
		Graphics::WaitForGPU();
		Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());
	}
}


// --------------------------------------------------------
// Prepares a new frame for the UI, feeding it fresh
// input and time information for this new frame.
// --------------------------------------------------------
void Game::UINewFrame(float deltaTime)
{
	// Feed fresh input data to ImGui
	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = deltaTime;
	io.DisplaySize.x = (float)Window::Width();
	io.DisplaySize.y = (float)Window::Height();

	// Reset the frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Determine new input capture
	Input::SetKeyboardCapture(io.WantCaptureKeyboard);
	Input::SetMouseCapture(io.WantCaptureMouse);
}

// --------------------------------------------------------
// Builds the UI for the current frame
// --------------------------------------------------------
void Game::BuildUI()
{
	// Should we show the built-in demo window?
	if (showUIDemoWindow)
	{
		ImGui::ShowDemoWindow();
	}

	// Actually build our custom UI, starting with a window
	ImGui::Begin("Inspector");
	{
		// Set a specific amount of space for widget labels
		ImGui::PushItemWidth(-160); // Negative value sets label width

		// === Overall details ===
		if (ImGui::TreeNode("App Details"))
		{
			ImGui::Spacing();
			ImGui::Text("Frame rate: %f fps", ImGui::GetIO().Framerate);
			ImGui::Text("Window Client Size: %dx%d", Window::Width(), Window::Height());

			// Should we show the demo window?
			if (ImGui::Button(showUIDemoWindow ? "Hide ImGui Demo Window" : "Show ImGui Demo Window"))
				showUIDemoWindow = !showUIDemoWindow;

			ImGui::Spacing();

			// Finalize the tree node
			ImGui::TreePop();
		}

		// === Multiple Render Targets ===
		if (ImGui::TreeNode("Render Targets"))
		{
			float width = ImGui::GetWindowWidth();
			ImVec2 size = ImVec2(
				width,
				width / Window::AspectRatio());

			for (unsigned int i = 0; i < 6; i++)
			{
				// Convert descriptor index BACK into actual GPU handle
				ImageWithHover(RenderTargets[i].SRV.GPUHandle, size);
			}

			ImageWithHover(RayTracing::RaytracingOutputUAV_GPU, size);

			// Finalize the tree node
			ImGui::TreePop();
		}


	}
	ImGui::End();
}

void Game::ImageWithHover(D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle, const ImVec2& size)
{
	// Draw the image
	ImGui::Image(ImTextureRef(gpuDescHandle.ptr), size);

	// Check for hover
	if (ImGui::IsItemHovered())
	{
		// Zoom amount and aspect of the image
		float zoom = 0.03f;
		float aspect = (float)size.x / size.y;

		// Get the coords of the image
		ImVec2 topLeft = ImGui::GetItemRectMin();
		ImVec2 bottomRight = ImGui::GetItemRectMax();

		// Get the mouse pos as a percent across the image, clamping near the edge
		ImVec2 mousePosGlobal = ImGui::GetMousePos();
		ImVec2 mousePos = ImVec2(mousePosGlobal.x - topLeft.x, mousePosGlobal.y - topLeft.y);
		ImVec2 uvPercent = ImVec2(mousePos.x / size.x, mousePos.y / size.y);

		uvPercent.x = max(uvPercent.x, zoom / 2);
		uvPercent.x = min(uvPercent.x, 1 - zoom / 2);
		uvPercent.y = max(uvPercent.y, zoom / 2 * aspect);
		uvPercent.y = min(uvPercent.y, 1 - zoom / 2 * aspect);

		// Figure out the uv coords for the zoomed image
		ImVec2 uvTL = ImVec2(uvPercent.x - zoom / 2, uvPercent.y - zoom / 2 * aspect);
		ImVec2 uvBR = ImVec2(uvPercent.x + zoom / 2, uvPercent.y + zoom / 2 * aspect);

		// Draw a floating box with a zoomed view of the image
		ImGui::BeginTooltip();
		ImGui::Image(ImTextureRef(gpuDescHandle.ptr), ImVec2(256, 256), uvTL, uvBR);
		ImGui::EndTooltip();
	}
}




// --------------------------------------------------------
// RayTrace draw loop
// --------------------------------------------------------
void Game::RayTrace(float deltaTime, float totalTime)
{
	// get current back buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer =
		Graphics::BackBuffers[Graphics::SwapChainIndex()];

	// Raytracing - create TLAS then trace it
	{
		RayTracing::CreateTopLevelAccelerationStructureForScene(entities);
		RayTracing::Raytrace(camera, skyBox->GetDescriptorIndex());
	}

	// Present
	{
		// must occure BEFORE present
		Graphics::CloseAndExecuteCommandList();

		// present current back buffer and move to next one
		bool vsync = Graphics::VsyncState();
		Graphics::SwapChain->Present(
			vsync ? 1 : 0,
			vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING
		);
		Graphics::AdvanceSwapChainIndex();

		// wait for GPU then reset allocator & cmd list
		Graphics::WaitForGPU();
		Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());
	}
}




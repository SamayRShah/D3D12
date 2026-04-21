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

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")

// For the DirectX Math library
using namespace DirectX;

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
}

void Game::CreateRootSigAndPipelineState()
{
	// load shaders
	Microsoft::WRL::ComPtr<ID3DBlob> vsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> psByteCode;

	// load shaders
	{
		D3DReadFileToBlob(FixPath(L"VS_PBR.cso").c_str(), vsByteCode.GetAddressOf());
		D3DReadFileToBlob(FixPath(L"PS_PBR.cso").c_str(), psByteCode.GetAddressOf());
	}

	// root signature
	{
		D3D12_ROOT_PARAMETER rootParams[1] = {};

		// root params
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[0].Constants.Num32BitValues = sizeof(DrawDescriptorIndices) / sizeof(unsigned int);
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

	// pipeline state
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
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

		// state
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

		psoDesc.SampleMask = 0xffffffff;
		Graphics::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.GetAddressOf()));
	}

	// setup scissor rect
	{
		// view port setup
		viewport = {};
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = (float)Window::Width();
		viewport.Height = (float)Window::Height();
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		// render to whole window
		scissorRect = {};
		scissorRect.left = 0;
		scissorRect.top = 0;
		scissorRect.right = Window::Width();
		scissorRect.bottom = Window::Height();
	}
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
	floorMat->SetRoughness(0.5f);
	floorMat->SetMetalness(0.5f);

	auto mandoMat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.6f, 0.5f));
	mandoMat->SetAlbedoIndex(Graphics::LoadTexture(ASSET(L"Textures/mando.png")));
	mandoMat->SetNormalMapIndex(Graphics::LoadTexture(ASSET(L"Textures/mando_normals.png")));
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
		auto mat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.8f, 0.4f));
		mat->SetMetalness(RandomRange(0.0f, 1.0f) * RandomRange(0.0f, 1.0f));
		mat->SetAlpha(0);
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
		auto mat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.8f, 0.4f));
		mat->SetRoughness(RandomRange(0.0f, 1.0f) * RandomRange(0.0f, 1.0f));
		mat->SetEmissive(RandomRange(1.0f, 5.0f));

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
		auto mat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.8f, 0.4f));
		mat->SetRoughness(RandomRange(0.0f, 1.0f) * RandomRange(0.0f, 1.0f));
		mat->SetEmissive(RandomRange(1.0f, 5.0f));

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
	// get current back buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer =
		Graphics::BackBuffers[Graphics::SwapChainIndex()];

	// clear render target
	{
		// Transition the back buffer from present to render target
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		rb.Transition.pResource = currentBackBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Graphics::CommandList->ResourceBarrier(1, &rb);

		// Background color for clearing
		float color[] = { 0,0,0,1 };

		// Clear the RTV
		Graphics::CommandList->ClearRenderTargetView(
			Graphics::RTVHandles[Graphics::SwapChainIndex()],
			color,
			0, 0); // No scissor rectangles

		// Clear the depth buffer, too
		Graphics::CommandList->ClearDepthStencilView(
			Graphics::DSVHandle,
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f,	// Max depth = 1.0f
			0,		// Not clearing stencil, but need a value
			0, 0);	// No scissor rects
	}
	
	// render
	{
		// set pipeline state & buffers
		Graphics::CommandList->SetPipelineState(pipelineState.Get());
		Graphics::CommandList->SetDescriptorHeaps(1, Graphics::CBVSRVDescriptorHeap.GetAddressOf());
		Graphics::CommandList->SetGraphicsRootSignature(rootSignature.Get());

		// render traget, viewport, and topology
		Graphics::CommandList->OMSetRenderTargets(1, &Graphics::RTVHandles[Graphics::SwapChainIndex()], true, &Graphics::DSVHandle);
		Graphics::CommandList->RSSetViewports(1, &viewport);
		Graphics::CommandList->RSSetScissorRects(1, &scissorRect);
		Graphics::CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// per frame data
		DrawDescriptorIndices drawData{};

		// vertex shader
		{
			VertexShaderPerFrameData vsFrame{};
			vsFrame.view = camera->GetView();
			vsFrame.projection = camera->GetProjection();

			D3D12_GPU_DESCRIPTOR_HANDLE cbHandleVS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
				(void*)(&vsFrame), sizeof(VertexShaderPerFrameData));

			drawData.vsPerFrameCBIndex = Graphics::GetDescriptorIndex(cbHandleVS);
		}

		// pixel shader
		{
			PixelShaderPerFrameData psFrame{};
			psFrame.cameraPosition = camera->GetTransform()->GetPosition();
			psFrame.lightCount = lightCount;
			memcpy(psFrame.lights, &lights[0], sizeof(Light) * lightCount);

			D3D12_GPU_DESCRIPTOR_HANDLE cbHandlePS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
				(void*)(&psFrame), sizeof(PixelShaderPerFrameData));

			drawData.psPerFrameCBIndex = Graphics::GetDescriptorIndex(cbHandlePS);
		}

		// entities
		for (std::shared_ptr<GameEntity> e: entities)
		{
			std::shared_ptr<Material> mat = e->GetMaterial();

			// Set the pipeline state for this material
			Graphics::CommandList->SetPipelineState(mat->GetPipelineState().Get());
			
			// add vb data
			drawData.vsVertexBufferIndex = Graphics::GetDescriptorIndex(e->GetMesh()->GetVertexBufferDescriptorHandle());

			// vs entity data
			{
				VertexShaderPerObjectData vsData = {};
				vsData.world = e->GetTransform()->GetWorldMatrix();
				vsData.worldInverseTranspose = e->GetTransform()->GetWorldInverseTransposeMatrix();

				D3D12_GPU_DESCRIPTOR_HANDLE cbHandleVS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
					(void*)(&vsData), sizeof(VertexShaderPerObjectData));

				drawData.vsPerObjectCBIndex = Graphics::GetDescriptorIndex(cbHandleVS);
			}

			// Pixel shader data and cbuffer setup
			{
				PixelShaderPerObjectData psData = {};
				psData.uvScale = mat->GetUVScale();
				psData.uvOffset = mat->GetUVOffset();
				psData.albedoIndex = mat->GetAlbedoIndex();
				psData.normalMapIndex = mat->GetNormalMapIndex();
				psData.roughnessIndex = mat->GetRoughnessIndex();
				psData.metalnessIndex = mat->GetMetalnessIndex();
				psData.color = mat->GetColorTint();
				psData.roughness = mat->GetRoughness();
				psData.metalness = mat->GetMetalness();

				D3D12_GPU_DESCRIPTOR_HANDLE cbHandlePS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
					(void*)(&psData), sizeof(PixelShaderPerObjectData));

				drawData.psPerObjectCBIndex = Graphics::GetDescriptorIndex(cbHandlePS);
			}

			Graphics::CommandList->SetGraphicsRoot32BitConstants(
				0,
				sizeof(DrawDescriptorIndices) / sizeof(unsigned int),
				&drawData,
				0);

			// get index buffer view
			std::shared_ptr<Mesh> mesh = e->GetMesh();
			D3D12_INDEX_BUFFER_VIEW  ibv = mesh->GetIndexBufferView();

			// set geometry and draw
			Graphics::CommandList->IASetIndexBuffer(&ibv);
			Graphics::CommandList->DrawIndexedInstanced((UINT)mesh->GetIndexCount(), 1, 0, 0, 0);
		}

		skyBox->Draw(camera);
	}

	// Present
	{
		// Transition back to present
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		rb.Transition.pResource = currentBackBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Graphics::CommandList->ResourceBarrier(1, &rb);

		// Must occur BEFORE present
		Graphics::CloseAndExecuteCommandList();

		// Present the current back buffer and move to the next one
		bool vsync = Graphics::VsyncState();
		Graphics::SwapChain->Present(
			vsync ? 1 : 0,
			vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
		Graphics::AdvanceSwapChainIndex();

		// Wait for the GPU to be done and then reset the command list & allocator
		Graphics::WaitForGPU();
		Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());
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
		RayTracing::Raytrace(camera, currentBackBuffer, skyBox->GetDescriptorIndex());
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




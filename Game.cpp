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

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")

// For the DirectX Math library
using namespace DirectX;

// random range helper macro
#define RandomRange(min, max) (float)rand() / RAND_MAX * (max - min) + min

// --------------------------------------------------------
// The constructor is called after the window and graphics API
// are initialized but before the game loop begins
// --------------------------------------------------------
Game::Game()
{
	// seed random
	srand((unsigned int)time(0));

	numLights = 22;
	GenerateLights();

	CreateRootSigAndPipelineState();
	CreateGeometry();

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


// --------------------------------------------------------
// Loads the two basic shaders, then creates the root signature 
// and pipeline state object for our very basic demo.
// --------------------------------------------------------
void Game::CreateRootSigAndPipelineState()
{
	// blobs to hold raw shader byte code
	Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderByteCode;

	// Load shaders
	{
		// Read compiled vs code into blob
		D3DReadFileToBlob(
			FixPath(L"vertexShader.cso").c_str(), vertexShaderByteCode.GetAddressOf());
		D3DReadFileToBlob(
			FixPath(L"pixelShader.cso").c_str(), pixelShaderByteCode.GetAddressOf());
	}

	// Input Layout
	const unsigned int inputElementCount = 4;
	D3D12_INPUT_ELEMENT_DESC inputElements[inputElementCount] = {};
	{
		// create input layout to describe vertex format

		// setup first element - 3 float value - position
		inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		inputElements[0].SemanticName = "POSITION";
		inputElements[0].SemanticIndex = 0;

		// UV (float2)
		inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
		inputElements[1].SemanticName = "TEXCOORD";
		inputElements[1].SemanticIndex = 0;

		// Normal (float3)
		inputElements[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		inputElements[2].SemanticName = "NORMAL";
		inputElements[2].SemanticIndex = 0;

		// Tangent (float3)
		inputElements[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[3].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		inputElements[3].SemanticName = "TANGENT";
		inputElements[3].SemanticIndex = 0;
	}

	// Root signature
	{
		// define VS cbv table
		D3D12_DESCRIPTOR_RANGE cbvRangeVS = {};
		cbvRangeVS.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		cbvRangeVS.NumDescriptors = 1;
		cbvRangeVS.BaseShaderRegister = 0;
		cbvRangeVS.RegisterSpace = 0;
		cbvRangeVS.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// define PS cbv table
		D3D12_DESCRIPTOR_RANGE cbvRangePS = {};
		cbvRangePS.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		cbvRangePS.NumDescriptors = 1;
		cbvRangePS.BaseShaderRegister = 0;
		cbvRangePS.RegisterSpace = 0;
		cbvRangePS.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// parameter
		D3D12_ROOT_PARAMETER rootParams[2];

		// vertex shader CBV table
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[0].DescriptorTable.pDescriptorRanges = &cbvRangeVS;

		// pixel shader CBV table
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &cbvRangePS;

		// Create a single static sampler (available to all pixel shaders)
		D3D12_STATIC_SAMPLER_DESC anisoWrap = {};
		anisoWrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		anisoWrap.Filter = D3D12_FILTER_ANISOTROPIC;
		anisoWrap.MaxAnisotropy = 16;
		anisoWrap.MaxLOD = D3D12_FLOAT32_MAX;
		anisoWrap.ShaderRegister = 0;  // Means register(s0) in the shader
		anisoWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC samplers[] = { anisoWrap };

		// describe & serialize root sig
		D3D12_ROOT_SIGNATURE_DESC rootSig = {};
		rootSig.Flags = 
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		rootSig.NumParameters = ARRAYSIZE(rootParams);
		rootSig.pParameters = rootParams;
		rootSig.NumStaticSamplers = ARRAYSIZE(samplers);
		rootSig.pStaticSamplers = samplers;

		ID3DBlob* serlializedRootSig = 0;
		ID3DBlob* errors = 0;

		D3D12SerializeRootSignature(
			&rootSig,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&serlializedRootSig,
			&errors
		);

		// check errors during init
		if (errors != 0)
			OutputDebugString((wchar_t*)errors->GetBufferPointer());

		// actually create root sig
		HRESULT hr = Graphics::Device->CreateRootSignature(
			0,
			serlializedRootSig->GetBufferPointer(),
			serlializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(rootSignature.GetAddressOf())
		);
	}

	// pipeline state
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// -- Input assembler related --
		psoDesc.InputLayout.NumElements = inputElementCount;
		psoDesc.InputLayout.pInputElementDescs = inputElements;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		// Overall primitive topology type (triangle, line, etc.) is set here 
		// IASetPrimTop() is still used to set list/strip/adj options

		// root signature
		psoDesc.pRootSignature = rootSignature.Get();

		// -- Shaders --
		psoDesc.VS.pShaderBytecode = vertexShaderByteCode->GetBufferPointer();
		psoDesc.VS.BytecodeLength = vertexShaderByteCode->GetBufferSize();
		psoDesc.PS.pShaderBytecode = pixelShaderByteCode->GetBufferPointer();
		psoDesc.PS.BytecodeLength = pixelShaderByteCode->GetBufferSize();

		// -- Render targets --
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		// -- States --
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

		// -- Misc --
		psoDesc.SampleMask = 0xffffff;

		// create piplines state object
		HRESULT hr = Graphics::Device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(pipelineState.GetAddressOf())
		);
	}

	// Setup viewport and scissor rect
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
	}
}

// --------------------------------------------------------
// Creates the geometry we're going to draw
// --------------------------------------------------------
void Game::CreateGeometry()
{
	// load textures
	std::wstring texturePath = L"../../Assets/Textures/";
	unsigned int paintAlbedo = Graphics::LoadTexture(FixPath(texturePath + L"PBR/paint_albedo.png").c_str());
	unsigned int paintNormals = Graphics::LoadTexture(FixPath(texturePath + L"PBR/paint_normals.png").c_str());
	unsigned int paintRoughness = Graphics::LoadTexture(FixPath(texturePath + L"PBR/paint_roughness.png").c_str());
	unsigned int paintMetal = Graphics::LoadTexture(FixPath(texturePath + L"PBR/paint_metal.png").c_str());

	unsigned int bronzeAlbedo = Graphics::LoadTexture(FixPath(texturePath + L"PBR/bronze_albedo.png").c_str());
	unsigned int bronzeNormals = Graphics::LoadTexture(FixPath(texturePath + L"PBR/bronze_normals.png").c_str());
	unsigned int bronzeRoughness = Graphics::LoadTexture(FixPath(texturePath + L"PBR/bronze_roughness.png").c_str());
	unsigned int bronzeMetal = Graphics::LoadTexture(FixPath(texturePath + L"PBR/bronze_metal.png").c_str());

	unsigned int snowAlbedo = Graphics::LoadTexture(FixPath(texturePath + L"PBR/snow_albedo.png").c_str());
	unsigned int snowNormals = Graphics::LoadTexture(FixPath(texturePath + L"PBR/snow_normals.png").c_str());
	unsigned int snowRoughness = Graphics::LoadTexture(FixPath(texturePath + L"PBR/snow_roughness.png").c_str());
	unsigned int snowMetal = Graphics::LoadTexture(FixPath(texturePath + L"PBR/snow_metal.png").c_str());

	unsigned int crateAlbedo = Graphics::LoadTexture(FixPath(texturePath + L"PBR/crate_wood_albedo.png").c_str());
	unsigned int crateNormals = Graphics::LoadTexture(FixPath(texturePath + L"PBR/crate_wood_normals.png").c_str());
	unsigned int crateRoughness = Graphics::LoadTexture(FixPath(texturePath + L"PBR/crate_wood_roughness.png").c_str());
	unsigned int crateMetal = Graphics::LoadTexture(FixPath(texturePath + L"PBR/crate_wood_metal.png").c_str());

	unsigned int mandoAlbedo = Graphics::LoadTexture(FixPath(texturePath + L"mando.png").c_str());
	unsigned int mandoNormals = Graphics::LoadTexture(FixPath(texturePath + L"mando_normals.png").c_str());

	auto painMat = std::make_shared<Material>(pipelineState);
	painMat->SetPBR(paintAlbedo, paintNormals, paintRoughness, paintMetal);

	auto mandoMat = std::make_shared<Material>(pipelineState);
	mandoMat->SetPBR(mandoAlbedo, mandoNormals, snowRoughness, snowMetal);

	auto bronzeMat = std::make_shared<Material>(pipelineState);
	bronzeMat->SetPBR(bronzeAlbedo, bronzeNormals, bronzeRoughness, bronzeMetal);

	auto snowMat = std::make_shared<Material>(pipelineState);
	snowMat->SetPBR(snowAlbedo, snowNormals, snowRoughness, snowMetal);

	auto crateMat = std::make_shared<Material>(pipelineState);
	crateMat->SetPBR(crateAlbedo, crateNormals, crateRoughness, crateMetal);

	// load meshes
	std::wstring meshPath = L"../../Assets/Meshes/";
	auto cube = std::make_shared<Mesh>("Cube", FixPath(meshPath + L"cube.obj"));
	auto sphere = std::make_shared<Mesh>("Sphere", FixPath(meshPath + L"sphere.obj"));
	auto helix = std::make_shared<Mesh>("Helix", FixPath(meshPath + L"helix.obj"));
	auto torus = std::make_shared<Mesh>("Torus", FixPath(meshPath + L"torus.obj"));
	auto mando = std::make_shared<Mesh>("Mando", FixPath(meshPath + L"mando.obj"));

	// create entities
	auto eCube = std::make_shared<GameEntity>(cube, bronzeMat);
	eCube->GetTransform()->SetPosition(-6, 0, 0);
	entities.push_back(eCube);

	auto eSphere = std::make_shared<GameEntity>(sphere, painMat);
	eSphere->GetTransform()->SetPosition(-3, 0, 0);
	entities.push_back(eSphere);

	auto eMando = std::make_shared<GameEntity>(mando, mandoMat);
	eMando->GetTransform()->SetPosition(0, 0, 0);
	entities.push_back(eMando);

	auto eHelix = std::make_shared<GameEntity>(helix, snowMat);
	eHelix->GetTransform()->SetPosition(3, 0, 0);
	entities.push_back(eHelix);

	auto eTorus = std::make_shared<GameEntity>(torus, crateMat);
	eTorus->GetTransform()->SetPosition(6, 0, 0);
	entities.push_back(eTorus);
}

// --------------------------------------------------------
// Generates (or regenerates) lights for the scene
// --------------------------------------------------------
void Game::GenerateLights()
{
	// Reset
	lights.clear();

	// Setup directional lights
	Light dir1 = {};
	dir1.Type = LIGHT_TYPE_DIRECTIONAL;
	dir1.Direction = XMFLOAT3(1, -2, 1);
	dir1.Color = XMFLOAT3(0.8f, 0.0f, 0.0f);
	dir1.Intensity = 3;

	Light dir2 = {};
	dir2.Type = LIGHT_TYPE_DIRECTIONAL;
	dir2.Direction = XMFLOAT3(-1, -2, -1);
	dir2.Color = XMFLOAT3(0.0f, 0.6f, 0.0f);
	dir2.Intensity = 3;

	Light dir3 = {};
	dir3.Type = LIGHT_TYPE_DIRECTIONAL;
	dir3.Direction = XMFLOAT3(0, 1, 0);
	dir3.Color = XMFLOAT3(0.0f, 0.0f, 0.8f);
	dir3.Intensity = 3;

	// Add light to the list
	lights.push_back(dir1);
	lights.push_back(dir2);
	lights.push_back(dir3);

	// Create the rest of the lights
	while (lights.size() < MAX_LIGHTS)
	{
		Light point = {};
		point.Type = LIGHT_TYPE_POINT;
		point.Position = XMFLOAT3(RandomRange(-7.0f, 7.0f), RandomRange(-7.0f, 7.0f), RandomRange(-1.0f, 1.0f));
		point.Color = XMFLOAT3(RandomRange(0, 1), RandomRange(0, 1), RandomRange(0, 1));
		point.Range = RandomRange(1.0f, 3.0f);
		point.Intensity = RandomRange(1, 15);

		// Add to the list
		lights.push_back(point);
	}

	// Make sure we're exactly MAX_LIGHTS big
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

	float offset = sinf(totalTime);

	size_t i = 0;
	for (auto& l : lights)
	{
		auto pos = l.Position;
		pos.z = offset * 2.0f * (i % 2 * 2.0f - 1.0f);
		l.Position = pos;
		i++;
	}

	i = 0;
	for (auto& e : entities)
	{
		e->GetTransform()->Rotate(0, deltaTime, 0);
		auto pos = e->GetTransform()->GetPosition();
		pos.y = offset * 2.0f * (i % 2 * 2.0f - 1.0f);
		e->GetTransform()->SetPosition(pos);
		i++;
	}
}


// --------------------------------------------------------
// Clear the screen, redraw everything, present to the user
// --------------------------------------------------------
void Game::Draw(float deltaTime, float totalTime)
{
	// get current back buffer
	Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer =
		Graphics::BackBuffers[Graphics::SwapChainIndex()];

	// clearing rt
	{
		// transition back buffer from present to target
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		rb.Transition.pResource = currentBackBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Graphics::CommandList->ResourceBarrier(1, &rb);

		// bg color
		float color[] = { 0, 0, 0, 1 };

		// clear RTV
		Graphics::CommandList->ClearRenderTargetView(
			Graphics::RTVHandles[Graphics::SwapChainIndex()],
			color,
			0, 0 // no scissor rects
		);

		// clear depth buffer
		Graphics::CommandList->ClearDepthStencilView(
			Graphics::DSVHandle,
			D3D12_CLEAR_FLAG_DEPTH,
			1.0f, // Max depth - 1
			0,    // Not clearing stencil but needs val
			0, 0  // No scissor rects
		);
	}

	// Render scene
	{
		// Set pipeline state
		Graphics::CommandList->SetPipelineState(pipelineState.Get());
		// set cb descriptor heap
		Graphics::CommandList->SetDescriptorHeaps(1, Graphics::CBSRVDescriptorHeap.GetAddressOf());
		// root sig (must happen before root descriptor table)
		Graphics::CommandList->SetGraphicsRootSignature(rootSignature.Get());

		// setup other render commands
		Graphics::CommandList->OMSetRenderTargets(1, &Graphics::RTVHandles[Graphics::SwapChainIndex()],true, &Graphics::DSVHandle);
		Graphics::CommandList->RSSetViewports(1, &viewport);
		Graphics::CommandList->RSSetScissorRects(1, &scissorRect);
		Graphics::CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// loop through entitites
		for (auto& e : entities)
		{
			// get material
			std::shared_ptr<Material> mat = e->GetMaterial();

			{
				// setup pso
				Graphics::CommandList->SetPipelineState(mat->GetPipelineState().Get());
			}

			{
				// fill out VS external data struct
				VertexShaderExternalData vsData = {};
				vsData.world = e->GetTransform()->GetWorldMatrix();
				vsData.worldInverseTranspose = e->GetTransform()->GetWorldInverseTransposeMatrix();
				vsData.view = camera->GetView();
				vsData.projection = camera->GetProjection();

				// copy struct to GPU and set handle
				D3D12_GPU_DESCRIPTOR_HANDLE cbHandleVS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
					(void*)(&vsData), sizeof(VertexShaderExternalData));
				Graphics::CommandList->SetGraphicsRootDescriptorTable(0, cbHandleVS);
			}

			{
				// fill out PS external data struct
				// Must match pixel shader definition!
				PixelShaderExternalData psData = {};
				psData.albedoIndex = mat->GetAlbedoIndex();
				psData.normalMapIndex = mat->GetNormalMapIndex();
				psData.roughnessIndex = mat->GetRoughnessIndex();
				psData.metalnessIndex = mat->GetMetalnessIndex();
				psData.uvScale = mat->GetUVScale();
				psData.uvOffset = mat->GetUVOffset();
				psData.numLights = numLights;
				memcpy(psData.lights, &lights[0], sizeof(Light) * MAX_LIGHTS);

				D3D12_GPU_DESCRIPTOR_HANDLE cbHandlePS = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(
					(void*)(&psData), sizeof(PixelShaderExternalData));
				Graphics::CommandList->SetGraphicsRootDescriptorTable(1, cbHandlePS);
			}

			// grab views from entity's mesh
			std::shared_ptr<Mesh> mesh = e->GetMesh();
			D3D12_INDEX_BUFFER_VIEW ibv = mesh->GetIndexBufferView();
			D3D12_VERTEX_BUFFER_VIEW vbv = mesh->GetVertexBufferView();

			// set views using cmd list
			Graphics::CommandList->IASetVertexBuffers(0, 1, &vbv);
			Graphics::CommandList->IASetIndexBuffer(&ibv);

			// draw indexed instanced
			Graphics::CommandList->DrawIndexedInstanced((UINT)mesh->GetIndexCount(), 1, 0, 0, 0);
		}
	}

	// Present
	{
		D3D12_RESOURCE_BARRIER rb = {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		rb.Transition.pResource = currentBackBuffer.Get();
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Graphics::CommandList->ResourceBarrier(1, &rb);

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
		Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());
	}
}




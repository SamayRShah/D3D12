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

// --------------------------------------------------------
// The constructor is called after the window and graphics API
// are initialized but before the game loop begins
// --------------------------------------------------------
Game::Game()
{
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
		// define cbv table
		D3D12_DESCRIPTOR_RANGE cbvTable = {};
		cbvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		cbvTable.NumDescriptors = 1;
		cbvTable.BaseShaderRegister = 0;
		cbvTable.RegisterSpace = 0;
		cbvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// parameter
		D3D12_ROOT_PARAMETER rootParam = {};
		rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParam.DescriptorTable.NumDescriptorRanges = 1;
		rootParam.DescriptorTable.pDescriptorRanges = &cbvTable;

		// describe & serialize root sig
		D3D12_ROOT_SIGNATURE_DESC rootSig = {};
		rootSig.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSig.NumParameters = 1;
		rootSig.pParameters = &rootParam;
		rootSig.NumStaticSamplers = 0;
		rootSig.pStaticSamplers = 0;

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
		Graphics::Device->CreateRootSignature(
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
		Graphics::Device->CreateGraphicsPipelineState(
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
	// load meshes
	std::wstring meshPath = L"..\\..\\Assets\\Meshes\\";
	auto cube = std::make_shared<Mesh>("Cube", FixPath(meshPath + L"cube.obj"));
	auto sphere = std::make_shared<Mesh>("Sphere", FixPath(meshPath + L"sphere.obj"));
	auto helix = std::make_shared<Mesh>("Helix", FixPath(meshPath + L"helix.obj"));
	auto torus = std::make_shared<Mesh>("Torus", FixPath(meshPath + L"torus.obj"));
	auto mando = std::make_shared<Mesh>("Mando", FixPath(meshPath + L"mando.obj"));

	// create entities
	auto eCube = std::make_shared<GameEntity>(cube);
	eCube->GetTransform()->SetPosition(-6, 0, 0);
	entities.push_back(eCube);

	auto eSphere = std::make_shared<GameEntity>(sphere);
	eSphere->GetTransform()->SetPosition(-3, 0, 0);
	entities.push_back(eSphere);

	auto eMando = std::make_shared<GameEntity>(mando);
	eMando->GetTransform()->SetPosition(0, 0, 0);
	entities.push_back(eMando);

	auto eHelix = std::make_shared<GameEntity>(helix);
	eHelix->GetTransform()->SetPosition(3, 0, 0);
	entities.push_back(eHelix);

	auto eTorus = std::make_shared<GameEntity>(torus);
	eTorus->GetTransform()->SetPosition(6, 0, 0);
	entities.push_back(eTorus);
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
		float color[] = { 0.4f, 0.6f, 0.75f, 1.0f };

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

		// root sig (must happen before root descriptor table)
		Graphics::CommandList->SetGraphicsRootSignature(rootSignature.Get());

		// set cb descriptor heap
		Graphics::CommandList->SetDescriptorHeaps(1, Graphics::CBSRVDescriptorHeap.GetAddressOf());

		// setup other render commands
		Graphics::CommandList->OMSetRenderTargets(1, &Graphics::RTVHandles[Graphics::SwapChainIndex()],true, &Graphics::DSVHandle);
		Graphics::CommandList->RSSetViewports(1, &viewport);
		Graphics::CommandList->RSSetScissorRects(1, &scissorRect);
		Graphics::CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// loop through entitites
		for (auto& e : entities)
		{
			// fill out vs external data struct
			VertexShaderExternalData vsData = {};
			vsData.mWorld = e->GetTransform()->GetWorldMatrix();
			vsData.mView = camera->GetView();
			vsData.mProjection = camera->GetProjection();

			// copy struct to GPU and set handle
			D3D12_GPU_DESCRIPTOR_HANDLE cbHandle = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle((void*)(&vsData), sizeof(VertexShaderExternalData));
			Graphics::CommandList->SetGraphicsRootDescriptorTable(0, cbHandle);

			// grab views from entity's mesh
			D3D12_INDEX_BUFFER_VIEW ibv = e->GetMesh()->GetIndexBufferView();
			D3D12_VERTEX_BUFFER_VIEW vbv = e->GetMesh()->GetVertexBufferView();

			// set views using cmd list
			Graphics::CommandList->IASetIndexBuffer(&ibv);
			Graphics::CommandList->IASetVertexBuffers(0, 1, &vbv);

			// draw indexed instanced
			Graphics::CommandList->DrawIndexedInstanced(e->GetMesh()->GetIndexCount(), 1, 0, 0, 0);
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
		Graphics::WaitForGPU();
		Graphics::ResetAllocatorAndCommandList();
	}
}




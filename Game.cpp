#include "Game.h"
#include "Graphics.h"
#include "Vertex.h"
#include "Input.h"
#include "PathHelpers.h"
#include "Window.h"

#include <DirectXMath.h>

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")
#include <d3dcompiler.h>

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
	const unsigned int inputElementCount = 2;
	D3D12_INPUT_ELEMENT_DESC inputElements[inputElementCount] = {};
	{
		// create input layout to describe vertex format

		// setup first element - 3 float value - position
		inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		inputElements[0].SemanticName = "POSITION";
		inputElements[0].SemanticIndex = 0;

		// setup second element - 4 float rgb value - color
		inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		inputElements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		inputElements[1].SemanticName = "COLOR";
		inputElements[1].SemanticIndex = 0;
	}

	// Root signature
	{
		// describe & serialize root sig
		D3D12_ROOT_SIGNATURE_DESC rootSig = {};
		rootSig.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSig.NumParameters = 0;
		rootSig.pParameters = 0;
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
	// temp color vars
	XMFLOAT4 red = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
	XMFLOAT4 green = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
	XMFLOAT4 blue = XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);

	// indices / vertices setup
	Vertex vertices[] =
	{
		{ XMFLOAT3(+0.0f, +0.5f, +0.0f), red },
		{ XMFLOAT3(+0.5f, -0.5f, +0.0f), blue },
		{ XMFLOAT3(-0.5f, -0.5f, +0.0f), green },
	};

	unsigned int indices[] = { 0, 1, 2 };

	// create buffers
	vertexBuffer = Graphics::CreateStaticBuffer(sizeof(Vertex), ARRAYSIZE(vertices), vertices);
	indexBuffer = Graphics::CreateStaticBuffer(sizeof(indices), ARRAYSIZE(indices), indices);

	// setup views
	vbView.StrideInBytes = sizeof(Vertex);
	vbView.SizeInBytes = sizeof(Vertex) * ARRAYSIZE(vertices);
	vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();

	ibView.Format = DXGI_FORMAT_R32_UINT;
	ibView.SizeInBytes = sizeof(unsigned int) * ARRAYSIZE(indices);
	ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
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
}


// --------------------------------------------------------
// Update your game here - user input, move objects, AI, etc.
// --------------------------------------------------------
void Game::Update(float deltaTime, float totalTime)
{
	// Example input checking: Quit if the escape key is pressed
	if (Input::KeyDown(VK_ESCAPE))
		Window::Quit();
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

		// setup other render commands
		Graphics::CommandList->OMSetRenderTargets(
			1, &Graphics::RTVHandles[Graphics::SwapChainIndex()],
			true, &Graphics::DSVHandle
		);
		Graphics::CommandList->RSSetViewports(1, &viewport);
		Graphics::CommandList->RSSetScissorRects(1, &scissorRect);
		Graphics::CommandList->IASetVertexBuffers(0, 1, &vbView);
		Graphics::CommandList->IASetIndexBuffer(&ibView);
		Graphics::CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// draw
		Graphics::CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
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




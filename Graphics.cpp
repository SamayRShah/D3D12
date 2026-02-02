#include "Graphics.h"
#include <dxgi1_6.h>

// Tell the drivers to use high-performance GPU in multi-GPU systems (like laptops)
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; // AMD
}

namespace Graphics
{
	// Annonymous namespace to hold variables
	// only accessible in this file
	namespace
	{
		bool apiInitialized = false;
		bool supportsTearing = false;
		bool vsyncDesired = false;
		BOOL isFullscreen = false;

		D3D_FEATURE_LEVEL featureLevel{};
		unsigned int currentBackBufferIndex = 0;
	}
}

// Getters
unsigned int Graphics::SwapChainIndex() { return currentBackBufferIndex; }
bool Graphics::VsyncState() { return vsyncDesired || !supportsTearing || isFullscreen; }
std::wstring Graphics::APIName()
{
	switch (featureLevel)
	{
	case D3D_FEATURE_LEVEL_10_0: return L"D3D10";
	case D3D_FEATURE_LEVEL_10_1: return L"D3D10.1";
	case D3D_FEATURE_LEVEL_11_0: return L"D3D11";
	case D3D_FEATURE_LEVEL_11_1: return L"D3D11.1";
	case D3D_FEATURE_LEVEL_12_0: return L"D3D12";
	case D3D_FEATURE_LEVEL_12_1: return L"D3D12.1";
	case D3D_FEATURE_LEVEL_12_2: return L"D3D12.2";
	default: return L"Unknown";
	}
}

// --------------------------------------------------------
// Initializes the Graphics API, which requires window details.
// 
// windowWidth     - Width of the window (and our viewport)
// windowHeight    - Height of the window (and our viewport)
// windowHandle    - OS-level handle of the window
// vsyncIfPossible - Sync to the monitor's refresh rate if available?
// --------------------------------------------------------
HRESULT Graphics::Initialize(unsigned int windowWidth, unsigned int windowHeight, HWND windowHandle, bool vsyncIfPossible)
{
	// Only init once
	if (apiInitialized) return E_FAIL;

	// save desired vsync state
	vsyncDesired = vsyncIfPossible;

	// Determine if screen tearing is possible
	Microsoft::WRL::ComPtr<IDXGIFactory5> factory;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
	{
		BOOL tearingSupported = false;
		HRESULT featureCheck = factory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&tearingSupported, sizeof(tearingSupported)
		);

		// final determination of tearing support
		supportsTearing = SUCCEEDED(featureCheck) && tearingSupported;
	}

	// Output debug info in debug mode
#if defined(DEBUG) || defined(_DEBUG)
	ID3D12Debug* debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	debugController->EnableDebugLayer();
#endif

	// create D3D12 device and check DirectX feature level
	{
		HRESULT createResult = D3D12CreateDevice(
			0, // not specifying which adapter (GPU)
			D3D_FEATURE_LEVEL_11_0, // min level
			IID_PPV_ARGS(Device.GetAddressOf())
		);

		if (FAILED(createResult)) return createResult;

		D3D_FEATURE_LEVEL levelsToCheck[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_2
		};

		D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
		levels.pFeatureLevelsRequested = levelsToCheck;
		levels.NumFeatureLevels = ARRAYSIZE(levelsToCheck);
		Device->CheckFeatureSupport(
			D3D12_FEATURE_FEATURE_LEVELS,
			&levels,
			sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)
		);
		featureLevel = levels.MaxSupportedFeatureLevel;
	}

#if defined(DEBUG) || defined(_DEBUG)
	// set debug message callback
	Device->QueryInterface(IID_PPV_ARGS(&InfoQueue));
#endif

	// Setup D3D12 command allocator / queue / list
	{
		// Setup Allocator
		Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(CommandAllocator.GetAddressOf())
		);

		// Command queue
		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(CommandQueue.GetAddressOf()));

		// Command List
		Device->CreateCommandList(
			0, // which physical GPU will handle task
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			CommandAllocator.Get(), // allocator for list
			0, // Initial pipeline state
			IID_PPV_ARGS(CommandList.GetAddressOf())
		);
	}

	// Swapchain Creation 
	{
		DXGI_SWAP_CHAIN_DESC swapDesc = {};
		swapDesc.BufferCount = NumBackBuffers;
		swapDesc.BufferDesc.Width = windowWidth;
		swapDesc.BufferDesc.Height = windowHeight;
		swapDesc.BufferDesc.RefreshRate.Numerator = 60;
		swapDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapDesc.Flags = supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		swapDesc.OutputWindow = windowHandle;
		swapDesc.SampleDesc.Count = 1;
		swapDesc.SampleDesc.Quality = 0;
		swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapDesc.Windowed = true;

		// Create DXGI factory to create swap chain
		Microsoft::WRL::ComPtr<IDXGIFactory> dxgiFactory;
		CreateDXGIFactory(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
		HRESULT swapResult = dxgiFactory->CreateSwapChain(
			CommandQueue.Get(), &swapDesc, SwapChain.GetAddressOf());
		if (FAILED(swapResult)) return swapResult;
	}

	// synchronization fence
	{
		Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(WaitFence.GetAddressOf()));
		WaitFenceEvent = CreateEventEx(0, 0, 0, EVENT_ALL_ACCESS);
		WaitFenceCounter = 0;
	}

	// api has been initialized
	apiInitialized = true;

	// Create descriptor heaps for back & depth buffers
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = NumBackBuffers;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(RTVHeap.GetAddressOf()));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(DSVHeap.GetAddressOf()));
	}

	// create initial buffers & descriptors
	ResizeBuffers(windowWidth, windowHeight);

	// wait for GPU
	WaitForGPU();
	return S_OK;
}

// --------------------------------------------------------
// Called at the end of the program to clean up any
// graphics API specific memory. 
// 
// This exists for completeness since D3D objects generally
// use ComPtrs, which get cleaned up automatically.  Other
// APIs might need more explicit clean up.
// --------------------------------------------------------
void Graphics::ShutDown()
{
}


// --------------------------------------------------------
// When the window is resized, the underlying 
// buffers (textures) must also be resized to match.
//
// If we don't do this, the window size and our rendering
// resolution won't match up.  This can result in odd
// stretching/skewing.
// 
// width  - New width of the window (and our viewport)
// height - New height of the window (and our viewport)
// --------------------------------------------------------
void Graphics::ResizeBuffers(unsigned int width, unsigned int height) 
{
	// API initialization guard
	if (!apiInitialized) return;

	// wait for GPU beore destroying / recreating resources
	WaitForGPU();

	// release buffers
	for (unsigned int i = 0; i < NumBackBuffers; i++)
		BackBuffers[i].Reset();

	SwapChain->ResizeBuffers(
		NumBackBuffers,
		width,
		height,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
	);

	// get increment size between RTV descriptors
	SIZE_T RTVDescriptorSize = (SIZE_T)Device->
		GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// setup backbuffers again (assuming descriptor heap already exists)
	for (unsigned int i = 0; i < NumBackBuffers; i++) 
	{
		// grab buffer from swapchain
		SwapChain->GetBuffer(i, IID_PPV_ARGS(BackBuffers[i].GetAddressOf()));

		// make a handle for it
		RTVHandles[i] = RTVHeap->GetCPUDescriptorHandleForHeapStart();
		RTVHandles[i].ptr += RTVDescriptorSize * (size_t)i;

		// create RTV 
		Device->CreateRenderTargetView(BackBuffers[i].Get(), 0, RTVHandles[i]);
	}

	// Reset Depth buffer & recreate it
	{
		DepthBuffer.Reset();

		// describe depth stencil buffer resource
		D3D12_RESOURCE_DESC depthBufferDesc = {};
		depthBufferDesc.Alignment = 0;
		depthBufferDesc.DepthOrArraySize = 1;
		depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthBufferDesc.Height = height;
		depthBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthBufferDesc.MipLevels = 1;
		depthBufferDesc.Width = width;
		depthBufferDesc.SampleDesc.Count = 1;
		depthBufferDesc.SampleDesc.Quality = 0;

		// most often used clear value
		D3D12_CLEAR_VALUE clear = {};
		clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		clear.DepthStencil.Depth = 1.0f;
		clear.DepthStencil.Stencil = 0;

		// describe memory heap that will house resource
		D3D12_HEAP_PROPERTIES props = {};
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.CreationNodeMask = 1;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.VisibleNodeMask = 1;

		// create resource and map resource to heap
		Device->CreateCommittedResource(
			&props,
			D3D12_HEAP_FLAG_NONE,
			&depthBufferDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear,
			IID_PPV_ARGS(DepthBuffer.GetAddressOf())
		);

		// Recreate DSV
		DSVHandle = DSVHeap->GetCPUDescriptorHandleForHeapStart();
		Device->CreateDepthStencilView(
			DepthBuffer.Get(),
			0, // Default view ( 1st mip )
			DSVHandle
		);

		// reset to first buffer
		currentBackBufferIndex = 0;

		// check fs state
		SwapChain->GetFullscreenState(&isFullscreen, 0);

		// wait for GPU
		WaitForGPU();
	}
}

// --------------------------------------------------------
// Helper for creating static buffer to get data once
// and remain immutable
// 
// dataStride - size of one piece of data
// dataCount - number of data
// data - pointer to data
// --------------------------------------------------------
Microsoft::WRL::ComPtr<ID3D12Resource> Graphics::CreateStaticBuffer(
	size_t dataStride, size_t dataCount, void* data
)
{
	// Create temporary command allocator list
	// Should only happen at start up, so refactor to reuse
	// existing lists and allocators for optimization
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> localAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> localList;

	Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(localAllocator.GetAddressOf())
	);

	Device->CreateCommandList(
		0, // Physical GPU To handle tasks
		D3D12_COMMAND_LIST_TYPE_DIRECT, // list type
		localAllocator.Get(), // allocator
		0, // initial pipeline state
		IID_PPV_ARGS(localList.GetAddressOf())
	);

	// overall buffer being created
	Microsoft::WRL::ComPtr<ID3D12Resource> finalBuffer;

	// describes final heap
	D3D12_HEAP_PROPERTIES props = {};
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.CreationNodeMask = 1;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1; // Assuming this is a regular buffer, not a texture
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = dataStride * dataCount; // Size of the buffer

	// starts as common state but will be implicitly transitioned to 
	// copy destination state
	Device->CreateCommittedResource(
		&props,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		0,
		IID_PPV_ARGS(finalBuffer.GetAddressOf())
	);

	// create intermediate upload heap for CPU->GPU data copy
	D3D12_HEAP_PROPERTIES uploadProps = {};
	uploadProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadProps.CreationNodeMask = 1;
	uploadProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	uploadProps.VisibleNodeMask = 1;

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
	Device->CreateCommittedResource(
		&uploadProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		0,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())
	);

	// map / memcpy / unmap
	void* gpuAddress = 0;
	uploadBuffer->Map(0, 0, &gpuAddress);
	memcpy(gpuAddress, data, dataStride * dataCount);
	uploadBuffer->Unmap(0, 0);

	// copy from upload heap to vert buffer
	localList->CopyResource(finalBuffer.Get(), uploadBuffer.Get());

	// Transition the buffer to generic read (was implicitly transitioned to copy_dest above)
	D3D12_RESOURCE_BARRIER rb = {};
	rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	rb.Transition.pResource = finalBuffer.Get();
	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; // Implicitly copy_dest now!
	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	localList->ResourceBarrier(1, &rb);

	// execute local command list, then return final buffer
	localList->Close();
	ID3D12CommandList* list[] = { localList.Get() };
	CommandQueue->ExecuteCommandLists(1, list);

	WaitForGPU();
	return finalBuffer;
}

// --------------------------------------------------------
// Advances swap chain backbuffer index by 1 and wraps to 0 
// when necessary after presenting current frame
// --------------------------------------------------------
void Graphics::AdvanceSwapChainIndex() 
{
	currentBackBufferIndex++;
	currentBackBufferIndex %= NumBackBuffers;
}

// --------------------------------------------------------
// Resets command allocator & list
// ** always wait before resetting
// --------------------------------------------------------
void Graphics::ResetAllocatorAndCommandList() 
{
	CommandAllocator->Reset();
	CommandList->Reset(CommandAllocator.Get(), 0);
}

// --------------------------------------------------------
// closes command list and sets GPU to work
// ** also wait for GPU finish to reset allocator & list
// --------------------------------------------------------
void Graphics::CloseAndExecuteCommandList() 
{
	CommandList->Close();
	ID3D12CommandList* lists[] = { CommandList.Get() };
	CommandQueue->ExecuteCommandLists(1, lists);
}


// --------------------------------------------------------
// Makes C++ code wait for GPU to finish executing
// --------------------------------------------------------
void Graphics::WaitForGPU() 
{
	// update fence value and place into GPU command queue
	WaitFenceCounter++;
	CommandQueue->Signal(WaitFence.Get(), WaitFenceCounter);

	// check if most recent value is < just set one
	if (WaitFence->GetCompletedValue() < WaitFenceCounter)
	{
		// tell fence to alert when hit, then wait for it
		WaitFence->SetEventOnCompletion(WaitFenceCounter, WaitFenceEvent);
		WaitForSingleObject(WaitFenceEvent, INFINITE);
	}
}

// --------------------------------------------------------
// Prints graphics debug messages waiting in the queue
// --------------------------------------------------------
void Graphics::PrintDebugMessages()
{
	// Do we actually have an info queue (usually in debug mode)
	if (!InfoQueue)
		return;

	// Any messages?
	UINT64 messageCount = InfoQueue->GetNumStoredMessages();
	if (messageCount == 0)
		return;

	// Loop and print messages
	for (UINT64 i = 0; i < messageCount; i++)
	{
		// Get the size so we can reserve space
		size_t messageSize = 0;
		InfoQueue->GetMessage(i, 0, &messageSize);

		// Reserve space for this message
		D3D12_MESSAGE* message = (D3D12_MESSAGE*)malloc(messageSize);
		InfoQueue->GetMessage(i, message, &messageSize);

		// Print and clean up memory
		if (message)
		{
			// Color code based on severity
			switch (message->Severity)
			{
			case D3D12_MESSAGE_SEVERITY_CORRUPTION:
			case D3D12_MESSAGE_SEVERITY_ERROR:
				printf("\x1B[91m"); break; // RED

			case D3D12_MESSAGE_SEVERITY_WARNING:
				printf("\x1B[93m"); break; // YELLOW

			case D3D12_MESSAGE_SEVERITY_INFO:
			case D3D12_MESSAGE_SEVERITY_MESSAGE:
				printf("\x1B[96m"); break; // CYAN
			}

			printf("%s\n\n", message->pDescription);
			free(message);

			// Reset color
			printf("\x1B[0m");
		}
	}

	// Clear any messages we've printed
	InfoQueue->ClearStoredMessages();
}

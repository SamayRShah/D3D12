#pragma once

#include <string>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#pragma comment (lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Graphics
{
	// --- CONSTANTS ---
	const unsigned int NumBackBuffers = 2;
	const unsigned int MaxConstantBuffers = 1000;
	const unsigned int MaxTextureDescriptors = 100;

	// --- GLOBAL VARS ---

	// Primary D3D12 API objects
	inline Microsoft::WRL::ComPtr<ID3D12Device> Device;
	inline Microsoft::WRL::ComPtr<IDXGISwapChain> SwapChain;

	// Command submission
	inline Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator[NumBackBuffers];
	inline Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue;
	inline Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;

	// constant buffers
	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CBVSRVDescriptorHeap;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> CBUploadHeap;

	// Rendering buffers & descriptors
	inline Microsoft::WRL::ComPtr<ID3D12Resource> BackBuffers[NumBackBuffers];
	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RTVHeap;
	inline D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[NumBackBuffers]{};

	inline Microsoft::WRL::ComPtr<ID3D12Resource> DepthBuffer;
	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DSVHeap;
	inline D3D12_CPU_DESCRIPTOR_HANDLE DSVHandle{};


	// Basic CPU/GPU synchronization
	inline Microsoft::WRL::ComPtr<ID3D12Fence> WaitFence;
	inline HANDLE WaitFenceEvent = 0;
	inline UINT64 WaitFenceCounter = 0;

	// Frame sync
	inline Microsoft::WRL::ComPtr<ID3D12Fence> FrameSyncFence;
	inline HANDLE FrameSyncFenceEvent;
	inline UINT64 FrameSyncFenceCounters[NumBackBuffers]{};

	// Debug Layer
	inline Microsoft::WRL::ComPtr<ID3D12InfoQueue> InfoQueue;

	// --- FUNCTIONS ---

	// Getters
	unsigned int SwapChainIndex();
	bool VsyncState();
	std::wstring APIName();

	// General functions
	HRESULT Initialize(unsigned int windowWidth, unsigned int windowHeight, HWND windowHandle, bool vsyncIfPossible);
	void ShutDown();
	void ResizeBuffers(unsigned int width, unsigned int height);
	void AdvanceSwapChainIndex();

	// Resource Creation
	unsigned int LoadTexture(const wchar_t* file, bool generateMips = true);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBuffer(size_t stride, size_t count, void* data);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(
		UINT64 size,
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		UINT64 alignment = 0,
		void* data = 0,
		size_t dataSize = 0
	);
	unsigned int CreateCubeMap(
		const wchar_t* right, const wchar_t* left,
		const wchar_t* up, const wchar_t* down,
		const wchar_t* front, const wchar_t* back
	);

	void ReserveDescriptorHeapSlot(
		D3D12_CPU_DESCRIPTOR_HANDLE* reservedCPUHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* reservedGPUHandle);

	unsigned int GetDescriptorIndex(D3D12_GPU_DESCRIPTOR_HANDLE handle);


	// Resource usage
	D3D12_GPU_DESCRIPTOR_HANDLE FillNextConstantBufferAndGetGPUDescriptorHandle(void* data, unsigned int dataSizeInBytes);

	// Command List & synchronization
	void ResetAllocatorAndCommandList(int index);
	void CloseAndExecuteCommandList();
	void WaitForGPU();

	// Debug Layer
	void PrintDebugMessages();
}
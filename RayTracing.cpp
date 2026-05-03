#include "RayTracing.h"
#include "Graphics.h"
#include "BufferStructs.h"
#include "Window.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>

namespace RayTracing
{
	// Annonymous namespace to hold variables
	// only accessible in this file
	namespace
	{
		bool dxrAvailable = false;
		bool dxrResourcesInitialized = false;

		// Shader table size tracking
		UINT64 missTableSize = 0;
		UINT64 missRecordSize = 0;
		UINT64 rayGenTableSize = 0;
		UINT64 rayGenRecordSize = 0;
		UINT64 hitGroupTableSize = 0;
		UINT64 hitGroupRecordSize = 0;

		// Track the size of various TLAS-related buffers
		// in the event they need to be resized later
		UINT64 tlasBufferSizeInBytes = 0;
		UINT64 tlasScratchSizeInBytes = 0;
		UINT64 tlasInstanceDataSizeInBytes[Graphics::NumBackBuffers]{};

		// Error messages
		const char* errorRaytracingNotSupported = "\nERROR: Raytracing not supported by the current graphics device.\n(On laptops, this may be due to battery saver mode.)\n";
		const char* errorDXRDeviceQueryFailed = "\nERROR: DXR Device query failed - DirectX Raytracing unavailable.\n";
		const char* errorDXRCommandListQueryFailed = "\nERROR: DXR Command List query failed - DirectX Raytracing unavailable.\n";
	}
}

// Makes use of integer division to ensure we are aligned to the proper multiple of "alignment"
#define ALIGN(value, alignment) (((value + alignment - 1) / alignment) * alignment)


// --------------------------------------------------------
// Check for raytracing support, prepare main API objects
// and create all necessary resources
// --------------------------------------------------------
HRESULT RayTracing::Initialize(
	unsigned int outputWidth,
	unsigned int outputHeight,
	std::wstring raytracingShaderLibraryFile)
{
	// Use CheckFeatureSupport to determine if ray tracing is supported
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 rtSupport = {};
	HRESULT supportResult = Graphics::Device->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5,
		&rtSupport,
		sizeof(rtSupport));

	// Query to ensure we can get proper versions of the device and command list
	HRESULT dxrDeviceResult = Graphics::Device->QueryInterface(IID_PPV_ARGS(DXRDevice.GetAddressOf()));
	HRESULT dxrCommandListResult = Graphics::CommandList->QueryInterface(IID_PPV_ARGS(DXRCommandList.GetAddressOf()));

	// Check the results
	if (FAILED(supportResult) || rtSupport.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) { printf("%s", errorRaytracingNotSupported); return supportResult; }
	if (FAILED(dxrDeviceResult)) { printf("%s", errorDXRDeviceQueryFailed); return dxrDeviceResult; }
	if (FAILED(dxrCommandListResult)) { printf("%s", errorDXRCommandListQueryFailed); return dxrCommandListResult; }
	
	// We have DXR support
	dxrAvailable = true;
	printf("\nDXR initialization success!\n");

	// Proceed with setup
	CreateRaytracingRootSignatures();
	CreateRaytracingPipelineState(raytracingShaderLibraryFile);
	CreateRaytracingOutputUAV(outputWidth, outputHeight);
	CreateShaderTables();
	dxrResourcesInitialized = true;
	return S_OK;
}


// --------------------------------------------------------
// Creates the root signatures necessary for raytracing:
//  - A global signature used across all shaders
//  - A local signature used for each ray hit
// --------------------------------------------------------
void RayTracing::CreateRaytracingRootSignatures()
{
	if (dxrResourcesInitialized || !dxrAvailable)
		return;

	// --- Root parameters ---
	D3D12_ROOT_PARAMETER rootParams[1] = {};

	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParams[0].Constants.Num32BitValues =
		sizeof(RayTracingDrawData) / sizeof(unsigned int);
	rootParams[0].Constants.RegisterSpace = 0;
	rootParams[0].Constants.ShaderRegister = 0;

	// --- Static sampler ---
	D3D12_STATIC_SAMPLER_DESC basicSampler = {};
	basicSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	basicSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	basicSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	basicSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	basicSampler.MaxLOD = D3D12_FLOAT32_MAX;
	basicSampler.ShaderRegister = 0;
	basicSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_STATIC_SAMPLER_DESC samplers[] = { basicSampler };

	// --- Root signature desc ---
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = ARRAYSIZE(rootParams);
	desc.pParameters = rootParams;
	desc.NumStaticSamplers = ARRAYSIZE(samplers);
	desc.pStaticSamplers = samplers;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

	// --- Create ---
	Microsoft::WRL::ComPtr<ID3DBlob> blob;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;

	D3D12SerializeRootSignature(
		&desc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		blob.GetAddressOf(),
		errors.GetAddressOf());

	DXRDevice->CreateRootSignature(
		1,
		blob->GetBufferPointer(),
		blob->GetBufferSize(),
		IID_PPV_ARGS(GlobalRaytracingRootSig.GetAddressOf()));
}
// --------------------------------------------------------
// Creates the raytracing pipeline state, which holds
// information about the shaders, payload, root signatures, etc.
// --------------------------------------------------------
void RayTracing::CreateRaytracingPipelineState(std::wstring raytracingShaderLibraryFile)
{
	if (dxrResourcesInitialized || !dxrAvailable)
		return;

	Microsoft::WRL::ComPtr<ID3DBlob> blob;
	D3DReadFileToBlob(raytracingShaderLibraryFile.c_str(), blob.GetAddressOf());

	std::vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.reserve(10);

	// === RayGen ===
	D3D12_EXPORT_DESC rayGenExport = { L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE };
	D3D12_DXIL_LIBRARY_DESC rayGenLib = { { blob->GetBufferPointer(), blob->GetBufferSize() }, 1, &rayGenExport };
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &rayGenLib });

	// === Miss ===
	D3D12_EXPORT_DESC missExports[2] = {
		{ L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE },
		{ L"MissShadow", nullptr, D3D12_EXPORT_FLAG_NONE }
	};
	D3D12_DXIL_LIBRARY_DESC missLib = { { blob->GetBufferPointer(), blob->GetBufferSize() }, 2, missExports };
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &missLib });

	// === Closest Hit ===
	D3D12_EXPORT_DESC hitExports[2] = {
		{ L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE },
		{ L"ClosestHitShadow", nullptr, D3D12_EXPORT_FLAG_NONE }
	};
	D3D12_DXIL_LIBRARY_DESC hitLib = { { blob->GetBufferPointer(), blob->GetBufferSize() }, 2, hitExports };
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &hitLib });

	// === HitGroup ===
	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
	hitGroupDesc.HitGroupExport = L"HitGroup";
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroupDesc });

	// === Shadow HitGroup ===
	D3D12_HIT_GROUP_DESC hitGroupShadowDesc = {};
	hitGroupShadowDesc.ClosestHitShaderImport = L"ClosestHitShadow";
	hitGroupShadowDesc.HitGroupExport = L"HitGroupShadow";
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroupShadowDesc });

	// === Shader Config ===
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
	shaderConfig.MaxPayloadSizeInBytes = sizeof(DirectX::XMFLOAT3) + sizeof(unsigned int) * 2;
	shaderConfig.MaxAttributeSizeInBytes = sizeof(DirectX::XMFLOAT2);

	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig });
	D3D12_STATE_SUBOBJECT* shaderConfigPtr = &subobjects.back();

	// === Association ===
	const wchar_t* shaderNames[] = {
		L"RayGen",
		L"Miss", L"MissShadow",
		L"HitGroup", L"HitGroupShadow"
	};

	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc = {};
	assoc.NumExports = _countof(shaderNames);
	assoc.pExports = shaderNames;
	assoc.pSubobjectToAssociate = shaderConfigPtr;

	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &assoc });

	// === Global Root Sig ===
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, GlobalRaytracingRootSig.GetAddressOf() });

	// === Pipeline Config ===
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
	pipelineConfig.MaxTraceRecursionDepth = 10;
	subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig });

	// === Create ===
	D3D12_STATE_OBJECT_DESC desc = {};
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.NumSubobjects = (UINT)subobjects.size();
	desc.pSubobjects = subobjects.data();

	DXRDevice->CreateStateObject(&desc, IID_PPV_ARGS(RaytracingPipelineStateObject.GetAddressOf()));
	RaytracingPipelineStateObject->QueryInterface(IID_PPV_ARGS(&RaytracingPipelineProperties));
}

// --------------------------------------------------------
// Sets up the shader table, which holds shader identifiers
// and local root signatures for all possible shaders
// used during raytracing.  Note that this is just a big
// chunk of GPU memory we need to manage ourselves.
// --------------------------------------------------------
void RayTracing::CreateShaderTables()
{
	if (dxrResourcesInitialized || !dxrAvailable)
		return;

	UINT64 rayGenCount = 1;
	UINT64 missCount = 2;
	UINT64 hitGroupCount = 2;

	// === RayGen ===
	rayGenRecordSize = ALIGN(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	rayGenTableSize = rayGenRecordSize * rayGenCount;

	RayGenTable = Graphics::CreateBuffer(rayGenTableSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

	unsigned char* addr = 0;
	RayGenTable->Map(0, 0, (void**)&addr);
	memcpy(addr, RaytracingPipelineProperties->GetShaderIdentifier(L"RayGen"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	RayGenTable->Unmap(0, 0);

	// === Miss ===
	missRecordSize = ALIGN(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	missTableSize = missRecordSize * missCount;

	MissTable = Graphics::CreateBuffer(missTableSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

	MissTable->Map(0, 0, (void**)&addr);
	memcpy(addr, RaytracingPipelineProperties->GetShaderIdentifier(L"Miss"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	addr += missRecordSize;
	memcpy(addr, RaytracingPipelineProperties->GetShaderIdentifier(L"MissShadow"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	MissTable->Unmap(0, 0);

	// === HitGroup ===
	hitGroupRecordSize = ALIGN(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	hitGroupTableSize = hitGroupRecordSize * hitGroupCount;

	HitGroupTable = Graphics::CreateBuffer(hitGroupTableSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

	HitGroupTable->Map(0, 0, (void**)&addr);
	memcpy(addr, RaytracingPipelineProperties->GetShaderIdentifier(L"HitGroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	addr += hitGroupRecordSize;
	memcpy(addr, RaytracingPipelineProperties->GetShaderIdentifier(L"HitGroupShadow"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	HitGroupTable->Unmap(0, 0);
}


// --------------------------------------------------------
// Creates a texture & wraps it with an Unordered Access View,
// allowing shaders to directly write into this memory.  The
// data in this texture will later be directly copied to the
// back buffer after raytracing is complete.
// --------------------------------------------------------
void RayTracing::CreateRaytracingOutputUAV(unsigned int width, unsigned int height)
{
	// Default heap for output buffer
	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapDesc.CreationNodeMask = 0;
	heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapDesc.VisibleNodeMask = 0;

	// Describe the final output resource (UAV)
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = width;
	desc.Height = height;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	DXRDevice->CreateCommittedResource(
		&heapDesc,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		0,
		IID_PPV_ARGS(RaytracingOutput.GetAddressOf()));

	// Do we have a UAV alrady?
	if (!RaytracingOutputUAV_GPU.ptr)
	{
		// Nope, so reserve a spot
		Graphics::ReserveDescriptorHeapSlot(
			&RaytracingOutputUAV_CPU,
			&RaytracingOutputUAV_GPU);
	}

	// Set up the UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	DXRDevice->CreateUnorderedAccessView(
		RaytracingOutput.Get(),
		0,
		&uavDesc,
		RaytracingOutputUAV_CPU);
}


// --------------------------------------------------------
// If the window size changes, so too should the output texture
// --------------------------------------------------------
void RayTracing::ResizeOutputUAV(
	unsigned int outputWidth,
	unsigned int outputHeight)
{
	if (!dxrResourcesInitialized || !dxrAvailable)
		return;

	// Wait for the GPU to be done
	Graphics::WaitForGPU();

	// Reset and re-created the buffer
	RaytracingOutput.Reset();
	CreateRaytracingOutputUAV(outputWidth, outputHeight);
}


// --------------------------------------------------------
// Creates a BLAS for a particular mesh.  
// 
// NOTE: This demo assumes exactly one BLAS, so running this 
// method more than once is not advised!
// --------------------------------------------------------
MeshRayTracingData RayTracing::CreateBottomLevelAccelerationStructureForMesh(Mesh* mesh)
{
	// Raytracing-related data for this mesh
	MeshRayTracingData rayTracingData{};

	// Don't bother if DXR isn't available
	if (!dxrAvailable)
		return rayTracingData;

	// Describe the geometry data we intend to store in this BLAS
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.VertexBuffer.StartAddress = mesh->GetVertexBuffer()->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = mesh->GetVertexBufferView().StrideInBytes;
	geometryDesc.Triangles.VertexCount = static_cast<UINT>(mesh->GetVertexCount());
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.IndexBuffer = mesh->GetIndexBuffer()->GetGPUVirtualAddress();
	geometryDesc.Triangles.IndexFormat = mesh->GetIndexBufferView().Format;
	geometryDesc.Triangles.IndexCount = static_cast<UINT>(mesh->GetIndexCount());
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE; // Performance boost when dealing with opaque geometry

	// Describe our overall input so we can get sizing info
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS accelStructInputs = {};
	accelStructInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	accelStructInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	accelStructInputs.pGeometryDescs = &geometryDesc;
	accelStructInputs.NumDescs = 1;
	accelStructInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO accelStructPrebuildInfo = {};
	DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&accelStructInputs, &accelStructPrebuildInfo);

	// Handle alignment requirements ourselves
	accelStructPrebuildInfo.ScratchDataSizeInBytes = ALIGN(accelStructPrebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	accelStructPrebuildInfo.ResultDataMaxSizeInBytes = ALIGN(accelStructPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

	// Create a scratch buffer so the device has a place to temporarily store data
	Microsoft::WRL::ComPtr<ID3D12Resource> BLASScratchBuffer = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ScratchDataSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Create the final buffer for the BLAS
	rayTracingData.BLAS = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ResultDataMaxSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Describe the final BLAS and set up the build
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = accelStructInputs;
	buildDesc.ScratchAccelerationStructureData = BLASScratchBuffer->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = rayTracingData.BLAS->GetGPUVirtualAddress();
	DXRCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, 0);

	// Set up a barrier to wait until the BLAS is actually built to proceed
	D3D12_RESOURCE_BARRIER blasBarrier = {};
	blasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	blasBarrier.UAV.pResource = rayTracingData.BLAS.Get();
	blasBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	DXRCommandList->ResourceBarrier(1, &blasBarrier);

	// Create two SRVs for the index and vertex buffers
	// Note: These must come one after the other in the descriptor heap, and index must come first
	//       This is due to the way we've set up the root signature (expects a table of these)
	D3D12_CPU_DESCRIPTOR_HANDLE ib_cpu, vb_cpu;
	Graphics::ReserveDescriptorHeapSlot(&ib_cpu, &rayTracingData.IndexBufferSRV);
	Graphics::ReserveDescriptorHeapSlot(&vb_cpu, &rayTracingData.VertexBufferSRV);

	// Index buffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC indexSRVDesc = {};
	indexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	indexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	indexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	indexSRVDesc.Buffer.StructureByteStride = 0;
	indexSRVDesc.Buffer.FirstElement = 0;
	indexSRVDesc.Buffer.NumElements = (UINT)mesh->GetIndexCount();
	indexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	DXRDevice->CreateShaderResourceView(mesh->GetIndexBuffer().Get(), &indexSRVDesc, ib_cpu);

	// Vertex buffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC vertexSRVDesc = {};
	vertexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	vertexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	vertexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	vertexSRVDesc.Buffer.StructureByteStride = 0;
	vertexSRVDesc.Buffer.FirstElement = 0;
	vertexSRVDesc.Buffer.NumElements = (UINT)((mesh->GetVertexCount() * sizeof(Vertex)) / sizeof(float)); // How many floats total?
	vertexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	DXRDevice->CreateShaderResourceView(mesh->GetVertexBuffer().Get(), &vertexSRVDesc, vb_cpu);

	// Finish up before moving on
	Graphics::CloseAndExecuteCommandList();
	Graphics::WaitForGPU();
	Graphics::ResetAllocatorAndCommandList(0);

	// Pass back the raytracing data for this mesh
	return rayTracingData;
}


// --------------------------------------------------------
// Creates the top level accel structure, which can be made
// up of one or more BLAS instances, each with their own
// unique transform.  This demo uses exactly one BLAS instance.
// --------------------------------------------------------
void RayTracing::CreateTopLevelAccelerationStructureForScene(std::vector<std::shared_ptr<GameEntity>> entities)
{
	// Don't bother if DXR isn't available or the AS is finalized already
	if (!dxrAvailable)
		return;

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs;
	for (std::shared_ptr<GameEntity> entity : entities)
	{
		// Grab the entity's transform and transpose to column major
		DirectX::XMFLOAT4X4 transform = entity->GetTransform()->GetWorldMatrix();
		XMStoreFloat4x4(&transform, XMMatrixTranspose(XMLoadFloat4x4(&transform)));

		// Describe the BLAS instance(s) that make up the TLAS
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceContributionToHitGroupIndex = entity->GetMaterial()->GetEmissive() > 0 ? 2 : entity->GetMaterial()->GetAlpha() <  0.8f ? 3 : 0;
		instanceDesc.InstanceMask = 0xFF;
		memcpy(&instanceDesc.Transform, &transform, sizeof(float) * 3 * 4); // Copy first [3][4] elements
		instanceDesc.AccelerationStructure = entity->GetMesh()->GetRayTracingData().BLAS->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		descs.push_back(instanceDesc);
	}

	// Grab the frame (back buffer) index.  Any CPU->GPU data
	// copies should be placed into a buffer that corresponds 
	// to the current back buffer index for sync purposes.
	unsigned int frameIndex = Graphics::SwapChainIndex();

	// The instance description actually needs to be in a buffer
	// on the GPU, so we need to make that buffer and toss it in
	// there ourselves (and keep the pointer long enough to finish the work)
	if (sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * descs.size() > tlasInstanceDataSizeInBytes[frameIndex])
	{
		// Reset and save the new size
		TLASInstanceDescBuffer[frameIndex].Reset();
		tlasInstanceDataSizeInBytes[frameIndex] = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * descs.size();

		// Create a new buffer to hold instance descriptions on the GPU
		TLASInstanceDescBuffer[frameIndex] = Graphics::CreateBuffer(
			tlasInstanceDataSizeInBytes[frameIndex],
			D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// Copy the description(s) into the new buffer
	unsigned char* mapped = 0;
	TLASInstanceDescBuffer[frameIndex]->Map(0, 0, (void**)&mapped);
	memcpy(mapped, descs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * descs.size());
	TLASInstanceDescBuffer[frameIndex]->Unmap(0, 0);

	// Describe our overall input so we can get sizing info
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS accelStructInputs = {};
	accelStructInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	accelStructInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	accelStructInputs.InstanceDescs = TLASInstanceDescBuffer[frameIndex]->GetGPUVirtualAddress();
	accelStructInputs.NumDescs = (unsigned int)descs.size();
	accelStructInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO accelStructPrebuildInfo = {};
	DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&accelStructInputs, &accelStructPrebuildInfo);

	// Handle alignment requirements ourselves
	accelStructPrebuildInfo.ScratchDataSizeInBytes = ALIGN(accelStructPrebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	accelStructPrebuildInfo.ResultDataMaxSizeInBytes = ALIGN(accelStructPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

	// Create a scratch buffer so the device has a place to temporarily store data
	if (accelStructPrebuildInfo.ScratchDataSizeInBytes > tlasScratchSizeInBytes)
	{
		// Reset and save current size
		TLASScratchBuffer.Reset();
		tlasScratchSizeInBytes = accelStructPrebuildInfo.ScratchDataSizeInBytes;

		// Create the new buffer
		TLASScratchBuffer = Graphics::CreateBuffer(
			tlasScratchSizeInBytes,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, 
				D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
		);
	}

	// Is our current tlas too small?
	if (accelStructPrebuildInfo.ResultDataMaxSizeInBytes > tlasBufferSizeInBytes)
	{
		// Create a new tlas buffer
		TLAS.Reset();
		tlasBufferSizeInBytes = accelStructPrebuildInfo.ResultDataMaxSizeInBytes;

		TLAS = Graphics::CreateBuffer(
			accelStructPrebuildInfo.ResultDataMaxSizeInBytes,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	}

	// Describe the final TLAS and set up the build
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = accelStructInputs;
	buildDesc.ScratchAccelerationStructureData = TLASScratchBuffer->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = TLAS->GetGPUVirtualAddress();
	DXRCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, 0);

	// Set up a barrier to wait until the TLAS is actually built to proceed
	// Note: Probably unnecessary because we're about to execute and wait below,
	//       but keeping this here in the event we adjust when we execute.
	D3D12_RESOURCE_BARRIER tlasBarrier = {};
	tlasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	tlasBarrier.UAV.pResource = TLAS.Get();
	tlasBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	DXRCommandList->ResourceBarrier(1, &tlasBarrier);

	// Reserve a descriptor if we haven't do so yet
	if (!TLASDescriptor_CPU.ptr)
		Graphics::ReserveDescriptorHeapSlot(&TLASDescriptor_CPU, &TLASDescriptor_GPU);

	// Update the descriptor
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.RaytracingAccelerationStructure.Location = TLAS->GetGPUVirtualAddress();
	Graphics::Device->CreateShaderResourceView(0, &srvDesc, TLASDescriptor_CPU);
}

// --------------------------------------------------------
// Creates the buffer for entity data read when ray tracing
// --------------------------------------------------------
void RayTracing::CreateEntityDataBuffer(std::vector<std::shared_ptr<GameEntity>> scene)
{
	// Set up entity data array
	std::vector<RayTracingEntityData> entityData;
	for (int i = 0; i < scene.size(); i++)
	{
		// Set up this entity's data
		RayTracingEntityData data{};
		std::shared_ptr<Material> mat = scene[i]->GetMaterial();
		DirectX::XMFLOAT3 c = scene[i]->GetMaterial()->GetColorTint();
		data.Color = DirectX::XMFLOAT4(c.x, c.y, c.z, 1);
		data.IndexBufferDescriptorIndex = Graphics::GetDescriptorIndex(scene[i]->GetMesh()->GetRayTracingData().IndexBufferSRV);
		data.VertexBufferDescriptorIndex = Graphics::GetDescriptorIndex(scene[i]->GetMesh()->GetRayTracingData().VertexBufferSRV);
		data.UVScale = mat->GetUVScale();
		data.UVOffset = mat->GetUVOffset();
		data.AlbedoIndex = mat->GetAlbedoIndex();
		data.NormalMapIndex = mat->GetNormalMapIndex();
		data.RoughnessIndex = mat->GetRoughnessIndex();
		data.MetalnessIndex = mat->GetMetalnessIndex();
		data.Roughness = mat->GetRoughness();
		data.Metalness = mat->GetMetalness();
		data.Emissive = mat->GetEmissive();
		data.IOR = mat->GetIOR();
		data.Alpha = mat->GetAlpha();
		entityData.push_back(data);
	}

	// How big will the buffer actually need to be?
	UINT64 bufferSize = sizeof(RayTracingEntityData) * entityData.size();

	// Reset the buffer if necessary, then create 
	// the new one and copy into it
	EntityDataStructuredBuffer.Reset();
	EntityDataStructuredBuffer = Graphics::CreateBuffer(
		bufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		0,
		&entityData[0],
		bufferSize);

	// Reserve slot in heap if necessary
	if (!EntityDataUAV_CPU.ptr)
		Graphics::ReserveDescriptorHeapSlot(&EntityDataUAV_CPU, &EntityDataUAV_GPU);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = (unsigned int)entityData.size();
	uavDesc.Buffer.StructureByteStride = sizeof(RayTracingEntityData);

	Graphics::Device->CreateUnorderedAccessView(
		EntityDataStructuredBuffer.Get(),
		0,
		&uavDesc,
		EntityDataUAV_CPU);
}


// --------------------------------------------------------
// Performs the actual raytracing work
// --------------------------------------------------------
void RayTracing::Raytrace(
	std::shared_ptr<Camera> camera,
	unsigned int skyboxDescriptorIndex
)
{
	if (!dxrResourcesInitialized || !dxrAvailable)
		return;

	// transition output UAV
	D3D12_RESOURCE_BARRIER rb = {};
	rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	rb.Transition.pResource = RaytracingOutput.Get();
	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	DXRCommandList->ResourceBarrier(1, &rb);

	// Set pipeline + root sig
	DXRCommandList->SetPipelineState1(RaytracingPipelineStateObject.Get());
	DXRCommandList->SetComputeRootSignature(GlobalRaytracingRootSig.Get());


	// Grab and fill a constant buffer
	RayTracingSceneData sceneData = {};
	sceneData.CameraPosition = camera->GetTransform()->GetPosition();
	sceneData.RaysPerPixel = raysPerPixel;

	DirectX::XMFLOAT4X4 view = camera->GetView();
	DirectX::XMFLOAT4X4 proj = camera->GetProjection();
	DirectX::XMMATRIX v = DirectX::XMLoadFloat4x4(&view);
	DirectX::XMMATRIX p = DirectX::XMLoadFloat4x4(&proj);
	DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(v, p);
	DirectX::XMStoreFloat4x4(&sceneData.InverseViewProjection, XMMatrixInverse(0, vp));

	D3D12_GPU_DESCRIPTOR_HANDLE cbuffer = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&sceneData, sizeof(RayTracingSceneData));

	// ACTUAL RAYTRACING HERE
	{
		// Set the CBV/SRV/UAV descriptor heap
		ID3D12DescriptorHeap* heap[] = { Graphics::CBVSRVDescriptorHeap.Get() };
		DXRCommandList->SetDescriptorHeaps(1, heap);

		// Set the pipeline state and root sig for raytracing
		DXRCommandList->SetPipelineState1(RaytracingPipelineStateObject.Get()); // Note the "1" on the function name
		DXRCommandList->SetComputeRootSignature(GlobalRaytracingRootSig.Get());

		// setup constant buffers
		RayTracingDrawData data{};
		data.SceneDataConstantBufferIndex = Graphics::GetDescriptorIndex(cbuffer);
		data.OutputUAVDescriptorIndex = Graphics::GetDescriptorIndex(RaytracingOutputUAV_GPU);
		data.EntityDataDescriptorIndex = Graphics::GetDescriptorIndex(EntityDataUAV_GPU);
		data.SceneTLASDescriptorIndex = Graphics::GetDescriptorIndex(TLASDescriptor_GPU);
		data.SkyboxDescriptorIndex = skyboxDescriptorIndex;

		DXRCommandList->SetComputeRoot32BitConstants(0, sizeof(RayTracingDrawData) / sizeof(unsigned int), &data, 0);

		// Dispatch rays
		D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

		// Set up dispatch shader table details
		{
			// Choose a specific ray gen shader (offset address to proper record)
			dispatchDesc.RayGenerationShaderRecord.StartAddress = RayGenTable->GetGPUVirtualAddress();
			dispatchDesc.RayGenerationShaderRecord.SizeInBytes = rayGenRecordSize;

			// Describe entire miss shader table
			dispatchDesc.MissShaderTable.StartAddress = MissTable->GetGPUVirtualAddress();
			dispatchDesc.MissShaderTable.SizeInBytes = missTableSize;
			dispatchDesc.MissShaderTable.StrideInBytes = missRecordSize;

			// Descrive entire hit group table
			dispatchDesc.HitGroupTable.StartAddress = HitGroupTable->GetGPUVirtualAddress();
			dispatchDesc.HitGroupTable.SizeInBytes = hitGroupTableSize;
			dispatchDesc.HitGroupTable.StrideInBytes = hitGroupRecordSize;
		}

		// Set number of rays to match screen size
		dispatchDesc.Width = Window::Width();
		dispatchDesc.Height = Window::Height();
		dispatchDesc.Depth = 1; // Can have a 3D grid, but we don't need that

		// GO!
		DXRCommandList->DispatchRays(&dispatchDesc);
	}

	// transition srv
	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	DXRCommandList->ResourceBarrier(1, &rb);
}

#pragma once

#include <string>

#include <d3d12.h>
#include <wrl/client.h>

#include "Vertex.h"

struct MeshRayTracingData
{
	D3D12_GPU_DESCRIPTOR_HANDLE IndexBufferSRV{};
	D3D12_GPU_DESCRIPTOR_HANDLE VertexBufferSRV{};
	Microsoft::WRL::ComPtr<ID3D12Resource> BLAS;
};

class Mesh
{
public:
	Mesh(const char* name, Vertex* vertArray, size_t numVerts, unsigned int* indexArray, size_t numIndices);
	Mesh(const char* name, const std::wstring& objFile);
	~Mesh() {}

	// Getters for mesh data
	const MeshRayTracingData& GetRayTracingData() { return rayTracingData; }

	D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() { return vbView; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetVertexBuffer() { return vertexBuffer; }
	D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() { return ibView; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetIndexBuffer() { return indexBuffer; }

	const char* GetName() { return name; }
	size_t GetIndexCount() { return numIndices; }
	size_t GetVertexCount() { return numVertices; }

private:
	// rt data
	MeshRayTracingData rayTracingData;

	// D3D buffers
	D3D12_VERTEX_BUFFER_VIEW vbView;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;

	D3D12_INDEX_BUFFER_VIEW ibView;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

	// Total indices & vertices in this mesh
	size_t numIndices;
	size_t numVertices;

	// Name (mostly for UI purposes)
	const char* name;

	// Helpers
	void CalculateTangents(Vertex* verts, size_t numVerts, unsigned int* indices, size_t numIndices);
	void CreateBuffers(Vertex* vertArray, size_t numVerts, unsigned int* indexArray, size_t numIndices);
};


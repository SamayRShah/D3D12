#pragma once

#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "ImGui/imgui.h"

#include "GameEntity.h"
#include "Camera.h"
#include "Lights.h"
#include "SkyBox.h"

enum RenderTargetType
{
	GBUFFER_ALBEDO,
	GBUFFER_NORMALS,
	GBUFFER_MATERIAL,
	GBUFFER_DEPTH,
	LIGHT_BUFFER,
	SDF,
	SCENE,

	// Count is always the last one!
	RENDER_TARGET_TYPE_COUNT
};


class Game
{
public:
	// Basic OOP setup
	Game();
	~Game();
	Game(const Game&) = delete; // Remove copy constructor
	Game& operator=(const Game&) = delete; // Remove copy-assignment operator

	// Primary functions
	void Update(float deltaTime, float totalTime);
	void Draw(float deltaTime, float totalTime);
	void RayTrace(float deltaTime, float totalTime);
	void OnResize();
private:

	// init helpers
	void CreateEntities();
	void CreateLights();
	void CreateRootSigAndPipelineState();

	// graphics  data
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	// raster pipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	// UI
	void UINewFrame(float deltaTime);
	void BuildUI();
	void ImageWithHover(D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle, const ImVec2& size);
	bool showUIDemoWindow;

	// scene
	unsigned int lightCount;
	std::vector<Light> lights;
	std::shared_ptr<FPSCamera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
	std::shared_ptr<SkyBox> skyBox;

	// defferred
	bool deferredLighting = false;

	// GBuffer
	static const unsigned int MaxRenderTargets = 10;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	TextureDetails RenderTargets[RENDER_TARGET_TYPE_COUNT]{};

	// Going to assume the same root signature is compatible with this PSO
	Microsoft::WRL::ComPtr<ID3D12PipelineState> fullScreenTexturePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> lightingPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sdfPSO;
public:
	// utils
	std::wstring AssetPath = L"../../Assets/";
};


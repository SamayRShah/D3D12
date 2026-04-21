#pragma once

#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "GameEntity.h"
#include "Camera.h"
#include "Lights.h"
#include "SkyBox.h"

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

	// scene
	unsigned int lightCount;
	std::vector<Light> lights;
	std::shared_ptr<FPSCamera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
	std::shared_ptr<SkyBox> skyBox;
public:
	// utils
	std::wstring AssetPath = L"../../Assets/";
};


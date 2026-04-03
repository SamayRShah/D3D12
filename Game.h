#pragma once

#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "GameEntity.h"
#include "Camera.h"
#include "Lights.h"

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
	void OnResize();
private:

	// Initialization helper methods - feel free to customize, combine, remove, etc.
	void CreateEntities();

	// graphics  data
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	// scene
	unsigned int skyboxDescriptorIndex = -1;
	std::shared_ptr<FPSCamera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
public:
	// utils
	std::wstring AssetPath = L"../../Assets/";

#define ASSET(asset) FixPath(AssetPath + asset).c_str()

#define SKY_ASSET(path) \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/right.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/left.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/up.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/down.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/front.png").c_str(), \
    FixPath(AssetPath + L"Textures/Skies/" + path + L"/back.png").c_str()  
};


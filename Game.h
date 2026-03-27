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

	std::shared_ptr<FPSCamera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
};


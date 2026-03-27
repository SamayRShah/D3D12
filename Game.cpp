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
#include "RayTracing.h"
#include "Utils.h"

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")

// For the DirectX Math library
using namespace DirectX;

// random range helper macro
#define RandomRange(min, max) (float)rand() / RAND_MAX * (max - min) + min

// --------------------------------------------------------
// The constructor is called after the window and graphics API
// are initialized but before the game loop begins
// --------------------------------------------------------
Game::Game()
{
	// seed random
	srand((unsigned int)time(0));

	// init ray tracing
	RayTracing::Initialize(
		Window::Width(), Window::Height(),
		FixPath(L"RayTracing.cso")
	);

	// create entities
	CreateEntities();

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
// Creates the geometry we're going to draw
// --------------------------------------------------------
void Game::CreateEntities()
{
	std::wstring AssetPath = L"../../Assets/";

	// Create materials
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState{};
	auto floorMat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.2f, 0.15f));
	auto mandoMat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.6f, 0.5f));

	// Load mesh(es)
	auto mandoMesh = std::make_shared<Mesh>("Mando", FixPath(AssetPath + L"Meshes/Mando.obj").c_str());
	auto cubeMesh = std::make_shared<Mesh>("Cube", FixPath(AssetPath + L"Meshes/cube.obj").c_str());

	std::vector<std::shared_ptr<Mesh>> meshes;
	auto torusMesh = std::make_shared<Mesh>("Torus", FixPath(AssetPath + L"Meshes/torus.obj").c_str());
	meshes.push_back(torusMesh);
	auto sphereMesh = std::make_shared<Mesh>("Sphere", FixPath(AssetPath + L"Meshes/sphere.obj").c_str());
	meshes.push_back(sphereMesh);
	auto crateMesh = std::make_shared<Mesh>("Crate", FixPath(AssetPath + L"Meshes/crate_wood.obj").c_str());
	meshes.push_back(crateMesh);
	auto helixMesh = std::make_shared<Mesh>("Helix", FixPath(AssetPath + L"Meshes/helix.obj").c_str());
	meshes.push_back(helixMesh);
	
	// Floor
	auto floor = std::make_shared<GameEntity>(cubeMesh, floorMat);
	floor->GetTransform()->SetScale(50);
	floor->GetTransform()->SetPosition(0, -51, 0);
	entities.push_back(floor);

	// Spinning mando
	auto mando = std::make_shared<GameEntity>(mandoMesh, mandoMat);
	mando->GetTransform()->SetPosition(0, 3, 0);
	entities.push_back(mando);

	for (int i = 0; i < 20; i++)
	{
		auto mat = std::make_shared<Material>(pipelineState, Utils::HSLColor(-1, 0.8f, 0.4f));

		float scale = RandomRange(0.25f, 0.6f);
	
		auto newEnt = std::make_shared<GameEntity>(meshes[(size_t)(RandomRange(0, meshes.size()))], mat);
		newEnt->GetTransform()->SetScale(scale);
		newEnt->GetTransform()->SetPosition(
			RandomRange(-9, 9),
			-1 + scale,
			RandomRange(-9, 9));
	
		entities.push_back(newEnt);
	}

	// Once we have all of the BLASs ready, we can make a TLAS
	RayTracing::CreateEntityDataBuffer(entities);
	RayTracing::CreateTopLevelAccelerationStructureForScene(entities);

	// finalize initialization and wait for GPU
	Graphics::CloseAndExecuteCommandList();
	Graphics::WaitForGPU();
	Graphics::ResetAllocatorAndCommandList(0);
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
	RayTracing::ResizeOutputUAV(Window::Width(), Window::Height());
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

	entities[1]->GetTransform()->Rotate(0, deltaTime * 0.5f, 0);
	entities[1]->GetTransform()->SetPosition(XMFLOAT3(0, sinf(totalTime) * 0.5f + 2.5f, 0));
	// Move stuff
	for (int i = 2; i < entities.size(); i++)
	{
		XMFLOAT3 pos = entities[i]->GetTransform()->GetPosition();
		switch (i % 2)
		{
		case 0:
			pos.x = sin((totalTime + i) * 0.4f) * 4;
			pos.y = sinf((totalTime + i) * 0.8f) + 1;
			break;

		case 1:
			pos.z = sin((totalTime + i) * 0.4f) * 4;
			pos.y = cosf((totalTime + i) * 0.8f) + 1;
			break;
		}
		entities[i]->GetTransform()->SetPosition(pos);
		entities[i]->GetTransform()->Rotate(deltaTime * 0.5f, deltaTime * 0.5f, deltaTime * 0.5f);
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

	// Raytracing - create TLAS then trace it
	{
		RayTracing::CreateTopLevelAccelerationStructureForScene(entities);
		RayTracing::Raytrace(camera, currentBackBuffer);
	}

	// Present
	{
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
		Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());
	}
}




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
	std::shared_ptr<Material> greyMat = std::make_shared<Material>(pipelineState, XMFLOAT3(0.5f, 0.5f, 0.5f));
	std::shared_ptr<Material> lightGreyMat = std::make_shared<Material>(pipelineState, XMFLOAT3(0.9f, 0.9f, 1));

	// Load mesh(es)
	std::shared_ptr<Mesh> cubeMesh = std::make_shared<Mesh>("Cube", FixPath(AssetPath + L"Meshes/cube.obj").c_str());
	std::shared_ptr<Mesh> torusMesh = std::make_shared<Mesh>("Torus", FixPath(AssetPath + L"Meshes/torus.obj").c_str());
	std::shared_ptr<Mesh> sphereMesh = std::make_shared<Mesh>("Sphere", FixPath(AssetPath + L"Meshes/sphere.obj").c_str());

	// Floor
	auto floor = std::make_shared<GameEntity>(cubeMesh, greyMat);
	floor->GetTransform()->SetScale(50);
	floor->GetTransform()->SetPosition(0, -51, 0);
	// entities.push_back(floor);

	// Spinning torus
	auto t = std::make_shared<GameEntity>(torusMesh, lightGreyMat);
	t->GetTransform()->SetScale(2);
	t->GetTransform()->SetPosition(0, 3, 0);
	// entities.push_back(t);

	auto sphere = std::make_shared<GameEntity>(sphereMesh, greyMat);
	entities.push_back(sphere);

	for (int i = 0; i < 20; i++)
	{
		auto mat = std::make_shared<Material>(pipelineState, XMFLOAT3(
			RandomRange(0.0f, 1.0f),
			RandomRange(0.0f, 1.0f),
			RandomRange(0.0f, 1.0f)));
	
		float scale = RandomRange(0.25f, 1.0f);
	
		auto sphereEnt = std::make_shared<GameEntity>(sphereMesh, mat);
		sphereEnt->GetTransform()->SetScale(scale);
		sphereEnt->GetTransform()->SetPosition(
			RandomRange(-6, 6),
			-1 + scale,
			RandomRange(-6, 6));
	
		// entities.push_back(sphereEnt);
	}

	// Create the ray tracing entity data buffer now that we have a scene
	// RayTracing::CreateEntityDataBuffer(entities);

	// Once we have all of the BLASs ready, we can make a TLAS
	RayTracing::CreateTopLevelAccelerationStructureForScene(entities[0]);

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
		RayTracing::CreateTopLevelAccelerationStructureForScene(entities[0]);
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




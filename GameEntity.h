#pragma once
#include <memory>

#include "Mesh.h"
#include "Transform.h"

class GameEntity {
public:
	GameEntity(std::shared_ptr<Mesh> mesh)
		: mesh(mesh) 
	{
		transform = std::make_shared<Transform>();
	}

	std::shared_ptr<Mesh> GetMesh() { return mesh; }
	std::shared_ptr<Transform> GetTransform() { return transform; }
	
	void SetMesh(std::shared_ptr<Mesh> mesh) { mesh = mesh; }
private:
	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<Transform> transform;
};
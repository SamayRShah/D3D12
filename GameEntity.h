#pragma once
#include <memory>

#include "Mesh.h"
#include "Transform.h"
#include "Material.h"

class GameEntity {
public:
	GameEntity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> mat)
		: mesh(mesh), material(mat)
	{
		transform = std::make_shared<Transform>();
	}

	// getters
	std::shared_ptr<Mesh> GetMesh() { return mesh; }
	std::shared_ptr<Transform> GetTransform() { return transform; }
	std::shared_ptr<Material> GetMaterial() { return material; }
	
	// setters
	void SetMesh(std::shared_ptr<Mesh> mesh) { mesh = mesh; }
	void SetMaterial(std::shared_ptr<Material> mat) { material = mat; }
private:
	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<Transform> transform;
	std::shared_ptr<Material> material;
};
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Sprite.h"

namespace MikuMikuWorld
{
	class Texture
	{
	public:
		Texture() = default;
		Texture(std::string textureName, int textureId, int textureWidth, int textureHeight)
			: name(std::move(textureName)), id(textureId), width(textureWidth), height(textureHeight) {}

		const std::string& getName() const { return name; }
		int getID() const { return id; }
		int getWidth() const { return width; }
		int getHeight() const { return height; }

		std::vector<Sprite> sprites;

	private:
		std::string name;
		int id{};
		int width{};
		int height{};
	};
}

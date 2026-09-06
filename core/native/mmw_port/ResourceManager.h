#pragma once

#include <map>
#include <string>
#include <vector>

#include "JsonIO.h"
#include "Particle.h"
#include "Rendering/Texture.h"

namespace MikuMikuWorld
{
	typedef std::map<int, Effect::Particle> ParticleIdMap;

	class ResourceManager
	{
	public:
		static std::vector<Texture> textures;

		static int getTexture(const std::string& name);
		static Effect::Particle& getParticleEffect(int id);
		static int getRootParticleIdByName(const std::string& name);
		static void loadEmbeddedEffects(int profile = 0);
		static void removeAllParticleEffects();

	private:
		static int nextParticleId;
		static ParticleIdMap particleIdMap;
		static std::map<std::string, int> effectNameToRootIdMap;

		static int readParticle(const nlohmann::json& j);
	};
}

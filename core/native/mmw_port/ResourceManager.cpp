#include "ResourceManager.h"

#include <limits>
#include <sstream>
#include <stdexcept>

#include "../generated/generated_resources.h"

using namespace nlohmann;

namespace MikuMikuWorld
{
	std::vector<Texture> ResourceManager::textures;
	int ResourceManager::nextParticleId{ 1 };
	ParticleIdMap ResourceManager::particleIdMap;
	std::map<std::string, int> ResourceManager::effectNameToRootIdMap;

	static Effect::KeyFrame readKeyFrame(const json& j)
	{
		float inTangent{}, outTangent{};
		if (jsonIO::keyExists(j, "inTangent"))
			inTangent = j["inTangent"] == "Infinity" ? std::numeric_limits<float>::infinity() : jsonIO::tryGetValue<float>(j, "inTangent", 0);
		if (jsonIO::keyExists(j, "outTangent"))
			outTangent = j["outTangent"] == "Infinity" ? std::numeric_limits<float>::infinity() : jsonIO::tryGetValue<float>(j, "outTangent", 0);

		return {
			jsonIO::tryGetValue<float>(j, "time", 0),
			jsonIO::tryGetValue<float>(j, "value", 0),
			inTangent,
			outTangent,
			jsonIO::tryGetValue<float>(j, "inWeight", 0),
			jsonIO::tryGetValue<float>(j, "outWeight", 0),
		};
	}

	static Effect::MinMax readMinMax(const json& j)
	{
		Effect::MinMax minmax;
		minmax.constant = j["constant"];
		minmax.min = j["randomMin"];
		minmax.max = j["randomMax"];
		minmax.mode = Effect::MinMaxMode(jsonIO::tryGetValue<int>(j, "mode", 0));

		if (jsonIO::keyExists(j, "curveMin"))
			for (const auto& entry : j["curveMin"])
				minmax.addKeyFrame(readKeyFrame(entry), Effect::MinMaxCurve::Min);
		if (jsonIO::keyExists(j, "curveMax"))
			for (const auto& entry : j["curveMax"])
				minmax.addKeyFrame(readKeyFrame(entry), Effect::MinMaxCurve::Max);

		minmax.sortKeyFrames();
		return minmax;
	}

	static Effect::MinMax3 readMinMax3(const json& j)
	{
		return { true, jsonIO::tryGetValue<bool>(j, "is3D"), readMinMax(j["x"]), readMinMax(j["y"]), readMinMax(j["z"]) };
	}

	static Effect::ColorKeyFrame readColorKeyFrame(const json& j)
	{
		return {
			jsonIO::tryGetValue<float>(j, "time", 0),
			Color{
				jsonIO::tryGetValue<float>(j, "r", 1.f),
				jsonIO::tryGetValue<float>(j, "g", 1.f),
				jsonIO::tryGetValue<float>(j, "b", 1.f),
				jsonIO::tryGetValue<float>(j, "a", 1.f),
			},
		};
	}

	static Effect::MinMaxColor readMinMaxColor(const json& j)
	{
		Effect::MinMaxColor minmax;
		minmax.mode = static_cast<Effect::MinMaxColorMode>(jsonIO::tryGetValue<int>(j, "mode", 0));
		minmax.min = jsonIO::tryGetValue(j, "colorMin", Color(1.f, 1.f, 1.f, 1.f));
		minmax.max = jsonIO::tryGetValue(j, "colorMax", Color(1.f, 1.f, 1.f, 1.f));
		minmax.constant = jsonIO::tryGetValue(j, "colorMax", Color(1.f, 1.f, 1.f, 1.f));

		if (jsonIO::keyExists(j, "gradientKeysMin"))
			for (const auto& entry : j["gradientKeysMin"])
				minmax.addKeyFrame(readColorKeyFrame(entry), Effect::MinMaxCurve::Min);
		if (jsonIO::keyExists(j, "gradientKeysMax"))
			for (const auto& entry : j["gradientKeysMax"])
				minmax.addKeyFrame(readColorKeyFrame(entry), Effect::MinMaxCurve::Max);

		minmax.sortKeyFrames();
		return minmax;
	}

	int ResourceManager::getTexture(const std::string& name)
	{
		for (int i = 0; i < static_cast<int>(textures.size()); ++i)
			if (textures[i].getName() == name)
				return i;
		return -1;
	}

	Effect::Particle& ResourceManager::getParticleEffect(int id)
	{
		return particleIdMap.at(id);
	}

	int ResourceManager::getRootParticleIdByName(const std::string& name)
	{
		return effectNameToRootIdMap.at(name);
	}

	int ResourceManager::readParticle(const json& j)
	{
		Effect::Particle p;
		p.name = j["name"];
		const json& transform = j["transform"];
		const Vector3 mainPosition = jsonIO::tryGetValue(transform, "position", Vector3());
		const Vector3 mainRotation = jsonIO::tryGetValue(transform, "rotation", Vector3());
		const Vector3 mainScale = jsonIO::tryGetValue(transform, "scale", Vector3());

		p.transform.position = { mainPosition.x, mainPosition.y, mainPosition.z, 1 };
		p.transform.rotation = { mainRotation.x, mainRotation.y, mainRotation.z, 1 };
		p.transform.scale = { mainScale.x, mainScale.y, mainScale.z, 1 };

		p.startSize = readMinMax3(j["startSize"]);
		p.startRotation = readMinMax3(j["startRotation"]);
		p.startDelay = readMinMax(j["startDelay"]);
		p.startLifeTime = readMinMax(j["startLifetime"]);
		p.startSpeed = readMinMax(j["startSpeed"]);
		p.startColor = readMinMaxColor(j["startColor"]);
		p.gravityModifier = readMinMax(j["gravityModifier"]);
		p.looping = jsonIO::tryGetValue<bool>(j, "loop", false);
		p.startDelay = readMinMax(j["startDelay"]);
		p.maxParticles = jsonIO::tryGetValue<int>(j, "maxParticles", 1);
		p.scalingMode = static_cast<Effect::ScalingMode>(jsonIO::tryGetValue<int>(j, "scalingMode", 1));
		p.duration = jsonIO::tryGetValue<float>(j, "duration", 1.f);
		p.flipRotation = jsonIO::tryGetValue<float>(j, "flipRotation", 0.f);
		p.simulationSpace = static_cast<Effect::TransformSpace>(jsonIO::tryGetValue<int>(j, "simulationSpace", 0));
		p.randomSeed = jsonIO::tryGetValue<uint32_t>(j, "randomSeed", 0);
		p.useAutoRandomSeed = jsonIO::tryGetValue<bool>(j, "useAutoRandomSeed", true);

		const json& emission = j["emission"];
		p.emission.rateOverTime = readMinMax(emission["rateOverTime"]);
		p.emission.rateOverDistance = readMinMax(emission["rateOverDistance"]);

		const json& emissionBursts = emission["bursts"];
		for (const auto& burst : emissionBursts)
		{
			p.emission.bursts.push_back({
				jsonIO::tryGetValue<float>(burst, "time", 0),
				jsonIO::tryGetValue<int>(burst, "countMax", 0),
				jsonIO::tryGetValue<int>(burst, "cycleCount", 0),
				jsonIO::tryGetValue<float>(burst, "repeatInterval", 0),
				jsonIO::tryGetValue<float>(burst, "probability", 1.f),
			});
		}

		const json& shape = j["shape"];
		const json& shapeTransform = shape["transform"];
		const Vector3 emitPosition = jsonIO::tryGetValue(shapeTransform, "position", Vector3());
		const Vector3 emitRotation = jsonIO::tryGetValue(shapeTransform, "rotation", Vector3());
		const Vector3 emitScale = jsonIO::tryGetValue(shapeTransform, "scale", Vector3());
		p.emission.transform.position = { emitPosition.x, emitPosition.y, emitPosition.z, 1 };
		p.emission.transform.rotation = { emitRotation.x, emitRotation.y, emitRotation.z, 1 };
		p.emission.transform.scale = { emitScale.x, emitScale.y, emitScale.z, 1 };
		p.emission.shape = static_cast<Effect::EmissionShape>(jsonIO::tryGetValue<int>(shape, "shapeType", 10));
		p.emission.radius = jsonIO::tryGetValue<float>(shape, "radius", 0);
		p.emission.radiusThickness = jsonIO::tryGetValue<float>(shape, "radiusThickness", 0);
		p.emission.angle = jsonIO::tryGetValue<float>(shape, "angle", 0);
		p.emission.arc = jsonIO::tryGetValue<float>(shape, "arc", 0);
		p.emission.arcSpeed = readMinMax(shape["arcSpeed"]);
		p.emission.randomizeDirection = jsonIO::tryGetValue<float>(shape, "randomizeDirection", 0);
		p.emission.randomizePosition = jsonIO::tryGetValue<float>(shape, "randomizePosition", 0);
		p.emission.spherizeDirection = jsonIO::tryGetValue<float>(shape, "spherizeDirection", 0);
		p.emission.arcMode = static_cast<Effect::ArcMode>(jsonIO::tryGetValue<int>(shape, "arcMode", 0));

		const json& textureSheet = j["textureSheetAnimation"];
		p.textureSplitX = jsonIO::tryGetValue<int>(textureSheet, "numTilesX", 1);
		p.textureSplitY = jsonIO::tryGetValue<int>(textureSheet, "numTilesY", 1);
		p.startFrame = readMinMax(textureSheet["startFrame"]);
		p.frameOverTime = readMinMax(textureSheet["frameOverTime"]);

		const json& renderer = j["renderer"];
		p.pivot = jsonIO::tryGetValue(renderer, "pivot", Vector3());
		p.order = jsonIO::tryGetValue<int>(renderer, "order", 50);
		p.speedScale = jsonIO::tryGetValue<float>(renderer, "speedScale");
		p.lengthScale = jsonIO::tryGetValue<float>(renderer, "lengthScale");
		p.renderMode = static_cast<Effect::RenderMode>(jsonIO::tryGetValue<int>(renderer, "mode", 0));
		p.alignment = static_cast<Effect::AlignmentMode>(jsonIO::tryGetValue<int>(renderer, "alignment", 0));

		const float blend = jsonIO::tryGetValue<float>(j["customData"], "blend", 0);
		p.blend = blend < 0.5f ? Effect::BlendMode::Typical : Effect::BlendMode::Additive;

		if (jsonIO::keyExists(j, "velocityOverLifetime"))
		{
			const json& velocityOverLifetime = j["velocityOverLifetime"];
			p.velocityOverLifetime = readMinMax3(velocityOverLifetime["linear"]);
			p.velocitySpace = static_cast<Effect::TransformSpace>(jsonIO::tryGetValue<int>(velocityOverLifetime, "space", 0));
			p.speedModifier = readMinMax(velocityOverLifetime["speedModifier"]);
		}
		else
		{
			p.speedModifier.mode = Effect::MinMaxMode::Constant;
			p.speedModifier.constant = 1.f;
		}

		if (jsonIO::keyExists(j, "limitVelocityOverLifetime"))
		{
			const json& limitVelocityOverLifetime = j["limitVelocityOverLifetime"];
			p.limitVelocityOverLifetime = readMinMax3(limitVelocityOverLifetime["speed"]);
			p.limitVelocityDampen = jsonIO::tryGetValue<float>(limitVelocityOverLifetime, "dampen", 0);
		}

		if (jsonIO::keyExists(j, "forceOverLifetime"))
		{
			const json& forceOverLifetime = j["forceOverLifetime"];
			p.forceOverLifetime = readMinMax3(forceOverLifetime["value"]);
			p.forceSpace = static_cast<Effect::TransformSpace>(jsonIO::tryGetValue<int>(forceOverLifetime, "space", 0));
		}

		if (jsonIO::keyExists(j, "colorOverLifetime"))
		{
			p.colorOverLifetime = readMinMaxColor(j["colorOverLifetime"]);
		}

		if (jsonIO::keyExists(j, "sizeOverLifetime"))
		{
			p.sizeOverLifetime = readMinMax3(j["sizeOverLifetime"]);
		}

		if (jsonIO::keyExists(j, "rotationOverLifetime"))
		{
			p.rotationOverLifetime = readMinMax3(j["rotationOverLifetime"]);
		}

		const int id = nextParticleId++;
		p.ID = id;
		particleIdMap[id] = p;

		if (jsonIO::keyExists(j, "children"))
		{
			for (const auto& child : j["children"])
			{
				const int childId = readParticle(child);
				particleIdMap[id].children.push_back(childId);
			}
		}

		return id;
	}

	void ResourceManager::loadEmbeddedEffects(int profile)
	{
		removeAllParticleEffects();
		textures.clear();
		textures.emplace_back("tex_note_common_all_v2", 3, 1024, 1024);

		if (profile == 1)
		{
			for (const auto& effect : mmw_preview::kEmbeddedEffectsProfile1)
			{
				const json root = json::parse(effect.json);
				const int rootId = readParticle(root);
				effectNameToRootIdMap[effect.name] = rootId;
			}
			return;
		}

		for (const auto& effect : mmw_preview::kEmbeddedEffectsProfile0)
		{
			const json root = json::parse(effect.json);
			const int rootId = readParticle(root);
			effectNameToRootIdMap[effect.name] = rootId;
		}
	}

	void ResourceManager::removeAllParticleEffects()
	{
		nextParticleId = 1;
		particleIdMap.clear();
		effectNameToRootIdMap.clear();
	}
}

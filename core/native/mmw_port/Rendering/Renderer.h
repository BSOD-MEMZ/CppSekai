#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "../Math.h"
#include "DirectXMath.h"
#include "Texture.h"

namespace MikuMikuWorld
{
	struct EffectOutputQuad
	{
		std::array<float, 8> positions{};
		std::array<float, 4> reciprocalW{};
		std::array<float, 8> uvs{};
		Color color{};
		int textureId{};
		int zIndex{};
	};

	class Renderer
	{
	public:
		Renderer() = default;

		void setEffectMatrices(const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projectionMatrix)
		{
			view = viewMatrix;
			projection = projectionMatrix;
		}

		void setOutput(std::vector<EffectOutputQuad>* outputQuads)
		{
			output = outputQuads;
		}

		void drawQuadWithBlend(const DirectX::XMMATRIX& m, const Texture& tex, int splitX, int splitY, int frame,
			const Color& tint, int z, float blend, int flipUVs)
		{
			if (!output || splitX <= 0 || splitY <= 0)
				return;

			static constexpr std::array<DirectX::XMFLOAT4, 4> kVertices{{
				{ 0.5f,  0.5f, 0.0f, 1.0f },
				{ 0.5f, -0.5f, 0.0f, 1.0f },
				{ -0.5f, -0.5f, 0.0f, 1.0f },
				{ -0.5f,  0.5f, 0.0f, 1.0f },
			}};

			EffectOutputQuad quad{};
			for (size_t i = 0; i < kVertices.size(); ++i)
			{
				DirectX::XMVECTOR value = DirectX::XMLoadFloat4(&kVertices[i]);
				value = DirectX::XMVector4Transform(value, m);
				value = DirectX::XMVector4Transform(value, view);
				value = DirectX::XMVector4Transform(value, projection);
				const float w = DirectX::XMVectorGetW(value);
				const float safeW = std::abs(w) > 0.000001f ? w : 1.0f;
				quad.positions[i * 2 + 0] = DirectX::XMVectorGetX(value) / safeW;
				quad.positions[i * 2 + 1] = DirectX::XMVectorGetY(value) / safeW;
				quad.reciprocalW[i] = 1.0f / safeW;
			}

			const int row = frame / splitX;
			const int col = frame % splitX;
			const float w = static_cast<float>(tex.getWidth()) / static_cast<float>(splitX);
			const float h = static_cast<float>(tex.getHeight()) / static_cast<float>(splitY);
			const float x1 = static_cast<float>(col) * w;
			const float x2 = x1 + w;
			const float y1 = static_cast<float>(row) * h;
			const float y2 = y1 + h;
			if (flipUVs)
			{
				quad.uvs = { x1, y1, x2, y1, x2, y2, x1, y2 };
			}
			else
			{
				quad.uvs = { x2, y1, x2, y2, x1, y2, x1, y1 };
			}

			quad.color = tint;
			quad.textureId = blend > 0.5f ? 4 : 3;
			quad.zIndex = z;
			output->push_back(quad);
		}

	private:
		DirectX::XMMATRIX view = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX projection = DirectX::XMMatrixIdentity();
		std::vector<EffectOutputQuad>* output{};
	};
}

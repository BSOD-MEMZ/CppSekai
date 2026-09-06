#pragma once

namespace MikuMikuWorld
{
	class Sprite
	{
	public:
		Sprite() = default;
		Sprite(float left, float right, float top, float bottom) : x1(left), x2(right), y1(top), y2(bottom) {}

		float getX1() const { return x1; }
		float getX2() const { return x2; }
		float getY1() const { return y1; }
		float getY2() const { return y2; }

	private:
		float x1{};
		float x2{};
		float y1{};
		float y2{};
	};
}

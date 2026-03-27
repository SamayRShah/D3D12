#pragma once
#include <DirectXMath.h>
#include <random>

namespace Utils
{
	float HueToRGB(float p, float q, float t)
	{
		if (t < 0) t += 1;
		if (t > 1) t -= 1;

		if (6.0f * t < 1.0f) return p + (q - p) * 6.0f * t;
		if (2.0f * t < 1.0f) return q;
		if (3.0f * t < 2.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;

		return 0;
	}

	DirectX::XMFLOAT3 HSLColor(float h = -1, float s = -1, float l = -1)
	{
		h = h < 0 ? (float)rand() / RAND_MAX : h;
		s = s < 0 ? (float)rand() / RAND_MAX : s;
		l = l < 0 ? (float)rand() / RAND_MAX : l;

		float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
		float p = 2.0f * l - q;

		float r = HueToRGB(p, q, h + 1.0f / 3.0f);
		float g = HueToRGB(p, q, h);
		float b = HueToRGB(p, q, h - 1.0f / 3.0f);

		return DirectX::XMFLOAT3(r, g, b);
	}
}
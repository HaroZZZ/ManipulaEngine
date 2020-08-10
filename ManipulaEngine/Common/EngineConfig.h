#pragma once

enum class RenderLayer : int
{
	Opaque = 0,
	Alpha = 1,
	Mirrors = 2,
	Transparent = 3,
	Count
};

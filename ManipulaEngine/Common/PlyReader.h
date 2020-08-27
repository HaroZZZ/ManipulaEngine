#pragma once
#include<vector>
#include "d3dUtil.h"


class PlyReader {
public:
	PlyReader();
	void ReadFile(const wchar_t* plyFileName);

	std::vector<VertexPos> Vertices;
	std::vector<std::uint16_t> Indices;
};
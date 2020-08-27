#include "PlyReader.h"
#include <iostream>
#include <fstream>

PlyReader::PlyReader()
{

}

void PlyReader::ReadFile(const wchar_t* plyFileName)
{
	std::wifstream wfin(plyFileName);
	if (!wfin.is_open())
		return;

	std::wstring wstr;
	int32_t VertexCount = 0;
	int32_t FaceCount = 0;

	while (1) {
		if(!(wfin >> wstr))
			break;
		//header
		if(wstr == L"ply")
			continue;
		else if (wstr == L"format" || wstr == L"comment" || wstr == L"property") {
			while (!wfin.eof() && wfin.get() != '\n')
				continue;
		}
		else if (wstr == L"element") {
			wfin >> wstr;
			if (wstr == L"vertex") {
				wfin >> VertexCount;
			}
			else if (wstr == L"face") {
				wfin >> FaceCount;
			}
		}
		else if (wstr == L"end_header") {
			break;
		}
	}

	//read vertex and index
	for (; VertexCount > 0; --VertexCount) {
		VertexPos v;
		wfin >> v.pos.x >> v.pos.y >> v.pos.z;
		Vertices.push_back(v);
	}
	for (; FaceCount > 0; --FaceCount) {
		std::uint16_t count,temp;
		wfin >> count;

		std::vector<int32_t> idxs;

		for (int32_t i = 0; i < count; ++i) {
			wfin >> temp;
			Indices.push_back(temp);
		}

	}

	wfin.close();
}

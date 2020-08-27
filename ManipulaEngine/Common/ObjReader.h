#include "d3dUtil.h"


class ObjReader
{
public:
	struct ObjPart
	{
		ObjPart() : material() {}
		~ObjPart() = default;

		Material material;
		std::vector<VertexPos> vertices;
		std::vector<WORD> indices16;
		std::vector<DWORD> indices32;
		std::wstring texStrDiffuse;
	};

	ObjReader() {}
	~ObjReader() = default;

	bool Read(const wchar_t* mboFileName, const wchar_t* objFileName);

	bool ReadObj(const wchar_t* objFileName);
	bool ReadMbo(const wchar_t* mboFileName);
	bool WriteMbo(const wchar_t* mboFileName);

public:
	std::vector<ObjPart> objParts;
	DirectX::XMFLOAT3 vMin, vMax;
private:
	void AddVertex(const VertexPosNormalTex& vertex, DWORD vpi, DWORD vti, DWORD vni);

	// »º´æÓÐv/vt/vn×Ö·û´®ÐÅÏ¢
	std::unordered_map<std::wstring, DWORD> vertexCache;
};


class MtlReader
{
public:
	bool ReadMtl(const wchar_t* mtlFileName);

public:
	std::map<std::wstring, Material> materials;
	std::map<std::wstring, std::wstring> mapKdStrs;
};
//////////////////////////////////////////////////////////////////////////
//
// the item need to render
//
//////////////////////////////////////////////////////////////////////////
#pragma once

#include "MathHelper.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include "d3dUtil.h"

const int gNumFrameResources = 3;


struct RenderItem {
	RenderItem() = default;
	
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};


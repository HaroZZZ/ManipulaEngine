#pragma once

#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "d3dApp.h"
#include "FrameResource.h"
#include "EngineConfig.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct RenderItem;

//extern const int gNumFrameResources;
//struct RenderItem;
//enum class RenderLayer : int;

class GameProgress : public D3DApp {
public:
	GameProgress(HINSTANCE hInstance);
	GameProgress(const GameProgress& rhs) = delete;
	GameProgress& operator=(const GameProgress& rhs) = delete;
	~GameProgress();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	//void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuileSourceBuffers();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildShpere();
	void BuildRoomGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvNormalDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	//std::unique_ptr<Waves> mWaves;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 inputPos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mSunTheta = 1.25f * XM_PI;
	float mSunPhi = XM_PIDIV4;

	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV2 - 0.1f;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

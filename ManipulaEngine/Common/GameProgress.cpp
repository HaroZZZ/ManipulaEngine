#include "GameProgress.h"
#include "RenderItem.h"

GameProgress::GameProgress(HINSTANCE hInstance):D3DApp(hInstance)
{

}

GameProgress::~GameProgress()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool GameProgress::Initialize()
{
	if (!D3DApp::Initialize())
		return false;
	//清空命令列表
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	//获取TYPE_CBV_SRV_UAV描述符的大小
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildBoxGeometry();
	BuildRoomGeometry();
	BuildMaterials();
	BuildRootSignature();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	//BuildConstantBuffers();
	BuileSourceBuffers();
	BuildShadersAndInputLayout();

	BuildPSOs();

	//执行初始化命令列表
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	//等待初始化完成
	FlushCommandQueue();

	return true;
}

void GameProgress::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void GameProgress::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);
	//准备渲染下一帧
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
	//等待gpu执行完当前帧的命令列表
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, NULL, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
	UpdateMaterialCBs(gt);
}

void GameProgress::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	//重写命令列表内存，只能当gpu执行完所有命令后重置
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	//标记资源状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	//清空后台缓存、深度缓存
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	//绑定根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	ID3D12DescriptorHeap* SrvdescriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(SrvdescriptorHeaps), SrvdescriptorHeaps);

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	//绘制item
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	//FlushCommandQueue();

	// 更新fence point
	mCurrFrameResource->Fence = ++mCurrentFence;

	// 设置新的fence point
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void GameProgress::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void GameProgress::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void GameProgress::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void GameProgress::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
		mSunTheta -= 1.f * dt;
	}

	if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
		mSunTheta += 1.f * dt;
	}


	if (GetAsyncKeyState(VK_UP) & 0x8000) {
		mSunPhi -= 1.f * dt;
	}

	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		mSunPhi += 1.f * dt;

	if (GetAsyncKeyState(0x57) & 0x8000) {//w
		inputPos.z += 5.f * dt;
	}
	if (GetAsyncKeyState(0x41) & 0x8000) {//a
		inputPos.x -= 5.f * dt;
	}
	if (GetAsyncKeyState(0x53) & 0x8000) {//s
		inputPos.z -= 5.f * dt;
	}
	if (GetAsyncKeyState(0x44) & 0x8000) {//d
		inputPos.x += 5.f * dt;
	}
	mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV4);
}

void GameProgress::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta) + inputPos.x;
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta) + inputPos.z;
	mEyePos.y = mRadius * cosf(mPhi);

	//mEyePos.x = inputPos.x;
	//mEyePos.z = inputPos.z;
	//mEyePos.y = 0.0f;

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void GameProgress::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();

	for (auto& e : mAllRitems) {
		XMMATRIX world = XMLoadFloat4x4(&e->World);
		ObjectConstants objConstants;
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
		currObjectCB->CopyData(e->ObjCBIndex, objConstants);

		// 更新下一个FrameResource
		e->NumFramesDirty--;
	}
}

void GameProgress::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void GameProgress::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.f, mSunTheta, mSunPhi);
	XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
	//mMainPassCB.Lights[0].Direction = lightDir;
	mMainPassCB.Lights[0].Strength = { 0.0f, 0.9f, 0.9f };
	//mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	//mMainPassCB.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
	//mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	//mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void GameProgress::LoadTextures()
{
	//创建纹理
	auto boxTex = std::make_unique<Texture>();
	boxTex->Name = "BoxTex";
	
	boxTex->Filename = L"../Textures/WireFence.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), boxTex->Filename.c_str(),
		boxTex->Resource, boxTex->UploadHeap));

	auto boxNTex = std::make_unique<Texture>();
	boxNTex->Name = "BoxNTex";
	boxNTex->Filename = L"../Textures/bricks_nmap.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), boxNTex->Filename.c_str(),
		boxNTex->Resource, boxNTex->UploadHeap));


	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"../Textures/bricks3.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto checkboardTex = std::make_unique<Texture>();
	checkboardTex->Name = "checkboardTex";
	checkboardTex->Filename = L"../Textures/checkboard.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), checkboardTex->Filename.c_str(),
		checkboardTex->Resource, checkboardTex->UploadHeap));

	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"../Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));


	mTextures[boxTex->Name] = std::move(boxTex);
	mTextures[boxNTex->Name] = std::move(boxNTex);
	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[checkboardTex->Name] = std::move(checkboardTex);
	mTextures[iceTex->Name] = std::move(iceTex);
}

void GameProgress::BuildRootSignature()
{
	//创建描述符表
	CD3DX12_DESCRIPTOR_RANGE texTable[2];
	texTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	texTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	CD3DX12_DESCRIPTOR_RANGE cbvTable2;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	//cbvTable1[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	cbvTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	//创建根参数，保存描述符表
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable[0], D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);//objconstant
	slotRootParameter[2].InitAsConstantBufferView(1);//pass
	slotRootParameter[3].InitAsConstantBufferView(2);//mat
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable[1], D3D12_SHADER_VISIBILITY_PIXEL);

	//slotRootParameter[1].InitAsConstantBufferView(0);
	//slotRootParameter[2].InitAsConstantBufferView(1);

	//获取采样器
	auto staticSamplers = GetStaticSamplers();

	//根签名描述结构
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	//创建根签名
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void GameProgress::BuildDescriptorHeaps()
{
	//创建CBV描述符堆
	//D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	//cbvHeapDesc.NumDescriptors = 2;
	//cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	//cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//cbvHeapDesc.NodeMask = 0;
	//ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
	//	IID_PPV_ARGS(&mCbvDescriptorHeap)));
	UINT objCount = (UINT)mAllRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvDescriptorHeap)));


	//SRV描述符堆
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	srvHeapDesc.NumDescriptors = 1;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvNormalDescriptorHeap)));
}

void GameProgress::BuildConstantBuffers()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mAllRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void GameProgress::BuileSourceBuffers()
{
	//CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	auto BoxTex = mTextures["BoxTex"]->Resource;
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto checkboardTex = mTextures["checkboardTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto BoxNTex = mTextures["BoxNTex"]->Resource;

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = BoxTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(BoxTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = bricksTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = checkboardTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	//加载法线贴图
	srvDesc.Format = BoxNTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(BoxNTex.Get(), &srvDesc, mSrvNormalDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void GameProgress::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};


	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["AlphaTest"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void GameProgress::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateSphere(1.0f, 20, 20);

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = 0;
	boxSubmesh.BaseVertexLocation = 0;

	std::vector<Vertex> vertices(box.Vertices.size());

	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices = box.GetIndices16();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto mBoxGeo = std::make_unique<MeshGeometry>();
	mBoxGeo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
	CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
	CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);

	mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);

	mBoxGeo->VertexByteStride = sizeof(Vertex);
	mBoxGeo->VertexBufferByteSize = vbByteSize;
	mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
	mBoxGeo->IndexBufferByteSize = ibByteSize;

	mBoxGeo->DrawArgs["box"] = boxSubmesh;


	mGeometries["boxGeo"] = std::move(mBoxGeo);
}

void GameProgress::BuildShpere()
{

}

void GameProgress::BuildRoomGeometry()
{
	std::array<Vertex, 20> vertices =
	{
		// Floor: Observe we tile texture coordinates.
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
		Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		// Wall: Observe we tile texture coordinates, and that we
		// leave a gap in the middle for the mirror.
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
		Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

		// Mirror
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	std::array<std::int16_t, 30> indices =
	{
		// Floor
		0, 1, 2,
		0, 2, 3,

		// Walls
		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// Mirror
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["floor"] = floorSubmesh;
	geo->DrawArgs["wall"] = wallSubmesh;
	geo->DrawArgs["mirror"] = mirrorSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void GameProgress::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));


	D3D12_GRAPHICS_PIPELINE_STATE_DESC AlphaPSO = opaquePsoDesc;
	AlphaPSO.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["AlphaTest"]->GetBufferPointer()),
		mShaders["AlphaTest"]->GetBufferSize()
	};
	AlphaPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&AlphaPSO, IID_PPV_ARGS(&mPSOs["alpha"])));
}

void GameProgress::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void GameProgress::BuildMaterials()
{
	auto woodCrate = std::make_unique<Material>();
	woodCrate->Name = "woodCrate";
	woodCrate->MatCBIndex = 0;
	woodCrate->DiffuseSrvHeapIndex = 0;
	woodCrate->NormalSrvHeapIndex = 0;
	woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	woodCrate->Roughness = 0.2f;

	auto bricks = std::make_unique<Material>();
	bricks->Name = "bricks";
	bricks->MatCBIndex = 1;
	bricks->DiffuseSrvHeapIndex = 1;
	bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	bricks->Roughness = 0.25f;

	auto checkertile = std::make_unique<Material>();
	checkertile->Name = "checkertile";
	checkertile->MatCBIndex = 2;
	checkertile->DiffuseSrvHeapIndex = 2;
	checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
	checkertile->Roughness = 0.3f;

	auto icemirror = std::make_unique<Material>();
	icemirror->Name = "icemirror";
	icemirror->MatCBIndex = 3;
	icemirror->DiffuseSrvHeapIndex = 3;
	icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->Roughness = 0.5f;

	mMaterials["bricks"] = std::move(bricks);
	mMaterials["checkertile"] = std::move(checkertile);
	mMaterials["woodCrate"] = std::move(woodCrate);
	mMaterials["icemirror"] = std::move(icemirror);
}

void GameProgress::BuildRenderItems()
{
	auto sphereRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem1->World, XMMatrixTranslation(-10.0f, 5.0f, 10.0f));
	//boxRitem->World = MathHelper::Identity4x4();
	sphereRitem1->ObjCBIndex = 0;
	//boxRitem->Mat = mMaterials["woodCrate"].get();
	sphereRitem1->Geo = mGeometries["boxGeo"].get();
	sphereRitem1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem1->IndexCount = sphereRitem1->Geo->DrawArgs["box"].IndexCount;
	sphereRitem1->StartIndexLocation = sphereRitem1->Geo->DrawArgs["box"].StartIndexLocation;
	sphereRitem1->BaseVertexLocation = sphereRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem1.get());

	auto sphereRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem2->World, XMMatrixTranslation(-5.0f, 5.0f, 10.0f));
	//boxRitem->World = MathHelper::Identity4x4();
	sphereRitem2->ObjCBIndex = 1;
	//boxRitem->Mat = mMaterials["woodCrate"].get();
	sphereRitem2->Geo = mGeometries["boxGeo"].get();
	sphereRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem2->IndexCount = sphereRitem2->Geo->DrawArgs["box"].IndexCount;
	sphereRitem2->StartIndexLocation = sphereRitem2->Geo->DrawArgs["box"].StartIndexLocation;
	sphereRitem2->BaseVertexLocation = sphereRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem2.get());

	auto sphereRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem3->World, XMMatrixTranslation(5.0f, 5.0f, 10.0f));
	//boxRitem->World = MathHelper::Identity4x4();
	sphereRitem3->ObjCBIndex = 2;
	//boxRitem->Mat = mMaterials["woodCrate"].get();
	sphereRitem3->Geo = mGeometries["boxGeo"].get();
	sphereRitem3->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem3->IndexCount = sphereRitem3->Geo->DrawArgs["box"].IndexCount;
	sphereRitem3->StartIndexLocation = sphereRitem3->Geo->DrawArgs["box"].StartIndexLocation;
	sphereRitem3->BaseVertexLocation = sphereRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem3.get());

	auto sphereRitem4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem4->World, XMMatrixTranslation(10.0f, 5.0f, 10.0f));
	//boxRitem->World = MathHelper::Identity4x4();
	sphereRitem4->ObjCBIndex = 3;
	//boxRitem->Mat = mMaterials["woodCrate"].get();
	sphereRitem4->Geo = mGeometries["boxGeo"].get();
	sphereRitem4->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem4->IndexCount = sphereRitem4->Geo->DrawArgs["box"].IndexCount;
	sphereRitem4->StartIndexLocation = sphereRitem4->Geo->DrawArgs["box"].StartIndexLocation;
	sphereRitem4->BaseVertexLocation = sphereRitem4->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem4.get());

	mAllRitems.push_back(std::move(sphereRitem1));
	mAllRitems.push_back(std::move(sphereRitem2));
	mAllRitems.push_back(std::move(sphereRitem3));
	mAllRitems.push_back(std::move(sphereRitem4));
}

void GameProgress::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		if (ri->Mat) {
			CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

			if (ri->Mat->NormalSrvHeapIndex > -1) {
				CD3DX12_GPU_DESCRIPTOR_HANDLE normal(mSrvNormalDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
				normal.Offset(ri->Mat->NormalSrvHeapIndex, mCbvSrvDescriptorSize);
				cmdList->SetGraphicsRootDescriptorTable(4, normal);
			}
			cmdList->SetGraphicsRootDescriptorTable(0, tex);
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
		}

		//tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		/*D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;*/
		//绑定描述符堆
		/*ID3D12DescriptorHeap* CbvdescriptorHeaps[] = { mCbvDescriptorHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(CbvdescriptorHeaps), CbvdescriptorHeaps);*/

		//CD3DX12_GPU_DESCRIPTOR_HANDLE obj(mCbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		//UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mAllRitems.size() + ri->ObjCBIndex;
		//obj.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
		//cmdList->SetGraphicsRootDescriptorTable(1, obj);

		//cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);

		//D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		//cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
	
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GameProgress::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}


#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include "Application.h"

using Microsoft::WRL::ComPtr;

void ThrowIfFailed(HRESULT Result)
{
	if (FAILED(Result)) throw std::runtime_error("HRESULT Failed");
}

class Application::ApplicationImplementation
{
public:
	ApplicationImplementation()
	{
		WCHAR PathToAssetsBuffer[512];
		GetModuleFileName(nullptr, PathToAssetsBuffer, _countof(PathToAssetsBuffer));
		WCHAR* LastSlash = wcsrchr(PathToAssetsBuffer, L'\\');
		if (LastSlash != nullptr)
		{
			*(LastSlash + 1) = L'\0';
		}
		PathToAssets = PathToAssetsBuffer;

		Viewport = { 0.0f, 0.0f, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };
		ScissorRectangle = { 0, 0, static_cast<LONG>(GetWidth()), static_cast<LONG>(GetHeight()) };
	}

	~ApplicationImplementation() = default;

	void SetWindow(HWND WindowHandle)
	{
		Window = WindowHandle;
	}

	void ParseCommandLineArguments(WCHAR* Arguments[], int NumberOfArguments)
	{

	}

	void Initialize()
	{
		UINT DxgiFactoryFlags = 0;
#ifdef _DEBUG
		{
			ComPtr<ID3D12Debug> DebugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
			{
				DebugController->EnableDebugLayer();
				DxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
		}
#endif

		ComPtr<IDXGIFactory5> Factory;
		ThrowIfFailed(CreateDXGIFactory2(DxgiFactoryFlags, IID_PPV_ARGS(&Factory)));

		// Create Device
		{
			ComPtr<IDXGIAdapter4> ChosenAdapter;

			SIZE_T MaximumVideoMemory = 0;
			ComPtr<IDXGIAdapter1> Adapter;
			DXGI_ADAPTER_DESC1 Description;
			for (UINT AdapterIndex = 0; Factory->EnumAdapters1(AdapterIndex, &Adapter) != DXGI_ERROR_NOT_FOUND; AdapterIndex++)
			{
				Adapter->GetDesc1(&Description);
				if ((Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;

				if (SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device3), nullptr)) &&
					Description.DedicatedVideoMemory > MaximumVideoMemory)
				{
					MaximumVideoMemory = Description.DedicatedVideoMemory;
					Adapter.As(&ChosenAdapter);
				}
			}

			ThrowIfFailed(D3D12CreateDevice(ChosenAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device)));
		}

		// Create Command Queue
		{
			D3D12_COMMAND_QUEUE_DESC QueueDescription = {};
			QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

			ThrowIfFailed(Device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(&CommandQueue)));
		}

		// Create Swap Chain
		{
			DXGI_SWAP_CHAIN_DESC1 SwapChainDescription = {};
			SwapChainDescription.BufferCount = FrameCount;
			SwapChainDescription.Width = GetWidth();
			SwapChainDescription.Height = GetHeight();
			SwapChainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			SwapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			SwapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			SwapChainDescription.SampleDesc.Count = 1;

			ComPtr<IDXGISwapChain1> TemporarySwapChain;
			ThrowIfFailed(Factory->CreateSwapChainForHwnd(
				CommandQueue.Get(),
				Window,
				&SwapChainDescription,
				nullptr,
				nullptr,
				&TemporarySwapChain
			));

			ThrowIfFailed(Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER));
			ThrowIfFailed(TemporarySwapChain.As(&SwapChain));
			CurrentFrameIndex = SwapChain->GetCurrentBackBufferIndex();
		}

		// Create Descriptor Heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC RenderTargetHeapDescription = {};
			RenderTargetHeapDescription.NumDescriptors = FrameCount;
			RenderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			RenderTargetHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			ThrowIfFailed(Device->CreateDescriptorHeap(&RenderTargetHeapDescription, IID_PPV_ARGS(&RenderTargetHeap)));
			RenderTargetDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		// Create Render Targets
		{
			auto Handle = RenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT FrameIndex = 0; FrameIndex < FrameCount; FrameIndex++)
			{
				ThrowIfFailed(SwapChain->GetBuffer(FrameIndex, IID_PPV_ARGS(&RenderTargets[FrameIndex])));
				Device->CreateRenderTargetView(RenderTargets[FrameIndex].Get(), nullptr, Handle);
				Handle.ptr += RenderTargetDescriptorSize;
			}
		}

		// Create Root Signature
		{
			D3D12_ROOT_SIGNATURE_DESC RootSignatureDescription;
			RootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			RootSignatureDescription.NumParameters = 0;
			RootSignatureDescription.pParameters = nullptr;
			RootSignatureDescription.NumStaticSamplers = 0;
			RootSignatureDescription.pStaticSamplers = nullptr;

			ComPtr<ID3DBlob> Signature;
			ComPtr<ID3DBlob> Error;
			ThrowIfFailed(D3D12SerializeRootSignature(&RootSignatureDescription, D3D_ROOT_SIGNATURE_VERSION_1, &Signature, &Error));
			ThrowIfFailed(Device->CreateRootSignature(0, Signature->GetBufferPointer(), Signature->GetBufferSize(), IID_PPV_ARGS(&RootSignature)));
		}

		// Create Pipeline State
		{
			ComPtr<ID3DBlob> VertexShader;
			ComPtr<ID3DBlob> PixelShader;

			UINT CompilerFlags = 0;

#ifdef _DEBUG
			CompilerFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

			ThrowIfFailed(D3DCompileFromFile(
				(PathToAssets + L"SimpleVertexShader.hlsl").c_str(),
				nullptr,
				nullptr,
				"Main",
				"vs_5_0",
				CompilerFlags,
				0,
				&VertexShader,
				nullptr
			));

			ThrowIfFailed(D3DCompileFromFile(
				(PathToAssets + L"SimplePixelShader.hlsl").c_str(),
				nullptr,
				nullptr,
				"Main",
				"ps_5_0",
				CompilerFlags,
				0,
				&PixelShader,
				nullptr
			));

			D3D12_INPUT_ELEMENT_DESC InputElementDescriptions[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			D3D12_RASTERIZER_DESC RasterizerDescription;
			RasterizerDescription.FillMode = D3D12_FILL_MODE_SOLID;
			RasterizerDescription.CullMode = D3D12_CULL_MODE_BACK;
			RasterizerDescription.FrontCounterClockwise = FALSE;
			RasterizerDescription.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
			RasterizerDescription.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
			RasterizerDescription.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
			RasterizerDescription.DepthClipEnable = TRUE;
			RasterizerDescription.MultisampleEnable = FALSE;
			RasterizerDescription.AntialiasedLineEnable = FALSE;
			RasterizerDescription.ForcedSampleCount = 0;
			RasterizerDescription.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

			D3D12_BLEND_DESC BlendDescription;
			BlendDescription.AlphaToCoverageEnable = FALSE;
			BlendDescription.IndependentBlendEnable = FALSE;
			const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDescription =
			{
				FALSE,FALSE,
				D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
				D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
				D3D12_LOGIC_OP_NOOP,
				D3D12_COLOR_WRITE_ENABLE_ALL,
			};
			for (UINT Index = 0; Index < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; Index++)
			{
				BlendDescription.RenderTarget[Index] = DefaultRenderTargetBlendDescription;
			}

			D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineStateDescription = {};
			PipelineStateDescription.InputLayout.pInputElementDescs = InputElementDescriptions;
			PipelineStateDescription.InputLayout.NumElements = _countof(InputElementDescriptions);
			PipelineStateDescription.pRootSignature = RootSignature.Get();
			PipelineStateDescription.VS.pShaderBytecode = VertexShader->GetBufferPointer();
			PipelineStateDescription.VS.BytecodeLength = VertexShader->GetBufferSize();
			PipelineStateDescription.PS.pShaderBytecode = PixelShader->GetBufferPointer();
			PipelineStateDescription.PS.BytecodeLength = PixelShader->GetBufferSize();
			PipelineStateDescription.RasterizerState = RasterizerDescription;
			PipelineStateDescription.BlendState = BlendDescription;
			PipelineStateDescription.DepthStencilState.DepthEnable = FALSE;
			PipelineStateDescription.DepthStencilState.StencilEnable = FALSE;
			PipelineStateDescription.SampleMask = UINT_MAX;
			PipelineStateDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			PipelineStateDescription.NumRenderTargets = 1;
			PipelineStateDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			PipelineStateDescription.SampleDesc.Count = 1;
			ThrowIfFailed(Device->CreateGraphicsPipelineState(&PipelineStateDescription, IID_PPV_ARGS(&PipelineState)));
		}

		//Create Command List and Allocator
		{
			ThrowIfFailed(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator)));
			ThrowIfFailed(Device->CreateCommandList(
				0, 
				D3D12_COMMAND_LIST_TYPE_DIRECT, 
				CommandAllocator.Get(), 
				PipelineState.Get(), 
				IID_PPV_ARGS(&CommandList)
			));
			ThrowIfFailed(CommandList->Close());
		}

		// Create Vertex Buffer
		{
			float AspectRatio = static_cast<float>(GetWidth()) / static_cast<float>(GetHeight());
			float Vertices[] =
			{
				 0.0f,   0.25f * AspectRatio, 0.0f, 1.0f,     1.0f, 0.0f, 0.0f, 1.0f,
				 0.25f, -0.25f * AspectRatio, 0.0f, 1.0f,     0.0f, 1.0f, 0.0f, 1.0f,
				-0.25f, -0.25f * AspectRatio, 0.0f, 1.0f,     0.0f, 0.0f, 1.0f, 1.0f
			};

			D3D12_HEAP_PROPERTIES UploadHeapProperties;
			UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			UploadHeapProperties.CreationNodeMask = 1;
			UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			UploadHeapProperties.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC VertexBufferDescription;
			VertexBufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			VertexBufferDescription.Format = DXGI_FORMAT_UNKNOWN;
			VertexBufferDescription.Width = sizeof(Vertices);
			VertexBufferDescription.Height = 1;
			VertexBufferDescription.Alignment = 0;
			VertexBufferDescription.DepthOrArraySize = 1;
			VertexBufferDescription.Flags = D3D12_RESOURCE_FLAG_NONE;
			VertexBufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			VertexBufferDescription.MipLevels = 1;
			VertexBufferDescription.SampleDesc.Count = 1;
			VertexBufferDescription.SampleDesc.Quality = 0;

			ThrowIfFailed(Device->CreateCommittedResource(
				&UploadHeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&VertexBufferDescription,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&VertexBuffer)
			));

			UINT8* VertexData;
			D3D12_RANGE ReadRange = {};
			ThrowIfFailed(VertexBuffer->Map(0, &ReadRange, reinterpret_cast<void**>(&VertexData)));
			memcpy(VertexData, Vertices, sizeof(Vertices));
			VertexBuffer->Unmap(0, nullptr);

			VertexBufferView.BufferLocation = VertexBuffer->GetGPUVirtualAddress();
			VertexBufferView.StrideInBytes = 8 * sizeof(float);
			VertexBufferView.SizeInBytes = sizeof(Vertices);
		}

		// Create Fence
		{
			ThrowIfFailed(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));
			FenceValue = 0;
			FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (FenceEvent == nullptr)
			{
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
			}
		}

		WaitForPreviousFrame();
	}

	void Update()
	{
	}

	void Render()
	{
		ThrowIfFailed(CommandAllocator->Reset());
		ThrowIfFailed(CommandList->Reset(CommandAllocator.Get(), PipelineState.Get()));

		// Fill Out Command List
		{
			CommandList->SetGraphicsRootSignature(RootSignature.Get());
			CommandList->RSSetViewports(1, &Viewport);
			CommandList->RSSetScissorRects(1, &ScissorRectangle);

			D3D12_RESOURCE_BARRIER PresentToRenderTargetBarrier;
			PresentToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			PresentToRenderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			PresentToRenderTargetBarrier.Transition.pResource = RenderTargets[CurrentFrameIndex].Get();
			PresentToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			PresentToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			PresentToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &PresentToRenderTargetBarrier);

			const float ClearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			auto RenderTargetHandle = RenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
			RenderTargetHandle.ptr += CurrentFrameIndex * RenderTargetDescriptorSize;
			CommandList->ClearRenderTargetView(RenderTargetHandle, ClearColor, 0, nullptr);
			CommandList->OMSetRenderTargets(1, &RenderTargetHandle, FALSE, nullptr);
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);
			CommandList->DrawInstanced(3, 1, 0, 0);

			D3D12_RESOURCE_BARRIER RenderTargetToPresentBarrier;
			RenderTargetToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			RenderTargetToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			RenderTargetToPresentBarrier.Transition.pResource = RenderTargets[CurrentFrameIndex].Get();
			RenderTargetToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			RenderTargetToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			RenderTargetToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &RenderTargetToPresentBarrier);
		}

		ThrowIfFailed(CommandList->Close());

		ID3D12CommandList* CommandLists[] = { CommandList.Get() };
		CommandQueue->ExecuteCommandLists(_countof(CommandLists), CommandLists);

		ThrowIfFailed(SwapChain->Present(1, 0));
		WaitForPreviousFrame();
	}

	void Dispose()
	{
		WaitForPreviousFrame();
		CloseHandle(FenceEvent);
	}

	void OnKeyPressed(UINT8 Key)
	{
	}

	void OnKeyReleased(UINT8 Key)
	{
	}

	UINT GetWidth() const { return 1280; }
	UINT GetHeight() const { return 720; }

private:
	HWND Window;
	std::wstring PathToAssets;

	D3D12_VIEWPORT Viewport = {};
	D3D12_RECT ScissorRectangle = {};

	static const UINT FrameCount = 2;
	ComPtr<IDXGISwapChain4> SwapChain;
	ComPtr<ID3D12Device3> Device;
	ComPtr<ID3D12Resource> RenderTargets[FrameCount];
	ComPtr<ID3D12CommandQueue> CommandQueue;
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> CommandList;
	ComPtr<ID3D12DescriptorHeap> RenderTargetHeap;
	ComPtr<ID3D12PipelineState> PipelineState;
	ComPtr<ID3D12RootSignature> RootSignature;

	ComPtr<ID3D12Resource> VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;

	UINT RenderTargetDescriptorSize = 0;
	UINT CurrentFrameIndex = 0;
	HANDLE FenceEvent = nullptr;
	ComPtr<ID3D12Fence1> Fence;
	UINT64 FenceValue = 0;

	void WaitForPreviousFrame()
	{
		ThrowIfFailed(CommandQueue->Signal(Fence.Get(), ++FenceValue));
		if (Fence->GetCompletedValue() < FenceValue)
		{
			ThrowIfFailed(Fence->SetEventOnCompletion(FenceValue, FenceEvent));
			WaitForSingleObject(FenceEvent, INFINITE);
		}
		CurrentFrameIndex = SwapChain->GetCurrentBackBufferIndex();
	}
};

Application::Application() :
	Implementation(std::make_unique<ApplicationImplementation>())
{
}

Application::~Application() = default;

void Application::SetWindow(HWND WindowHandle)
{
	Implementation->SetWindow(WindowHandle);
}

void Application::ParseCommandLineArguments(WCHAR* Arguments[], int NumberOfArguments)
{
	Implementation->ParseCommandLineArguments(Arguments, NumberOfArguments);
}

void Application::Initialize()
{
	Implementation->Initialize();
}

void Application::Update()
{
	Implementation->Update();
}

void Application::Render()
{
	Implementation->Render();
}

void Application::Dispose()
{
	Implementation->Dispose();
}

void Application::OnKeyPressed(UINT8 Key)
{
	Implementation->OnKeyPressed(Key);
}

void Application::OnKeyReleased(UINT8 Key)
{
	Implementation->OnKeyReleased(Key);
}

const WCHAR* Application::GetWindowTitle() const
{
	return L"DirectX 12 Experiment";
}

UINT Application::GetWidth() const
{
	return Implementation->GetWidth();
}

UINT Application::GetHeight() const
{
	return Implementation->GetHeight();
}
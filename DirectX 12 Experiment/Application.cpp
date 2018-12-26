#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "Application.h"

using Microsoft::WRL::ComPtr;

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
		CreateDXGIFactory2(DxgiFactoryFlags, IID_PPV_ARGS(&Factory));

		// Create Device
		{
			ComPtr<IDXGIAdapter4> ChosenAdapter;

			SIZE_T MaximumVideoMemory = 0;
			ComPtr<IDXGIAdapter1> Adapter;
			DXGI_ADAPTER_DESC1 Description;
			for (UINT AdapterIndex = 0; Factory->EnumAdapters1(AdapterIndex, &Adapter) != DXGI_ERROR_NOT_FOUND; AdapterIndex++)
			{
				Adapter->GetDesc1(&Description);
				if (Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
				if (SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)) &&
					Description.DedicatedVideoMemory > MaximumVideoMemory)
				{
					MaximumVideoMemory = Description.DedicatedVideoMemory;
					Adapter.As(&ChosenAdapter);
				}
			}

			D3D12CreateDevice(ChosenAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device));
		}

		// Create Command Queue
		{
			D3D12_COMMAND_QUEUE_DESC QueueDescription = {};
			QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

			Device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(&CommandQueue));
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
			Factory->CreateSwapChainForHwnd(
				CommandQueue.Get(),
				Window,
				&SwapChainDescription,
				nullptr,
				nullptr,
				&TemporarySwapChain
			);

			Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
			TemporarySwapChain.As(&SwapChain);
			CurrentFrameIndex = SwapChain->GetCurrentBackBufferIndex();
		}

		// Create Descriptor Heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC RenderTargetHeapDescription = {};
			RenderTargetHeapDescription.NumDescriptors = FrameCount;
			RenderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			RenderTargetHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			Device->CreateDescriptorHeap(&RenderTargetHeapDescription, IID_PPV_ARGS(&RenderTargetHeap));
			RenderTargetDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		// Create Render Targets
		{
			D3D12_CPU_DESCRIPTOR_HANDLE Handle = RenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT FrameIndex = 0; FrameIndex < FrameCount; FrameIndex++)
			{
				SwapChain->GetBuffer(FrameIndex, IID_PPV_ARGS(&RenderTargets[FrameIndex]));
				Device->CreateRenderTargetView(RenderTargets[FrameIndex].Get(), nullptr, Handle);
				Handle.ptr += RenderTargetDescriptorSize;
			}
		}

		//Create Command List and Allocator
		{
			Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator));
			Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&CommandList));
			CommandList->Close();
		}

		// Create Fence
		{
			Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
			FenceValue = 0;
			FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		}
	}

	void Update()
	{
	}

	void Render()
	{
		CommandAllocator->Reset();
		CommandList->Reset(CommandAllocator.Get(), PipelineState.Get());

		// Fill Out Command List
		{
			D3D12_RESOURCE_BARRIER PresentToRenderTargetBarrier = {};
			PresentToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			PresentToRenderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			PresentToRenderTargetBarrier.Transition.pResource = RenderTargets[CurrentFrameIndex].Get();
			PresentToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			PresentToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			PresentToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &PresentToRenderTargetBarrier);

			const float ClearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetHandle = RenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
			RenderTargetHandle.ptr += CurrentFrameIndex * RenderTargetDescriptorSize;
			CommandList->ClearRenderTargetView(RenderTargetHandle, ClearColor, 0, nullptr);

			D3D12_RESOURCE_BARRIER RenderTargetToPresentBarrier = {};
			RenderTargetToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			RenderTargetToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			RenderTargetToPresentBarrier.Transition.pResource = RenderTargets[CurrentFrameIndex].Get();
			RenderTargetToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			RenderTargetToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			RenderTargetToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			CommandList->ResourceBarrier(1, &RenderTargetToPresentBarrier);
		}

		CommandList->Close();

		ID3D12CommandList* CommandLists[] = { CommandList.Get() };
		CommandQueue->ExecuteCommandLists(_countof(CommandLists), CommandLists);

		SwapChain->Present(1, 0);
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

	static const UINT FrameCount = 2;
	ComPtr<IDXGISwapChain4> SwapChain;
	ComPtr<ID3D12Device> Device;
	ComPtr<ID3D12Resource1> RenderTargets[FrameCount];
	ComPtr<ID3D12CommandQueue> CommandQueue;
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> CommandList;
	ComPtr<ID3D12DescriptorHeap> RenderTargetHeap;
	ComPtr<ID3D12PipelineState> PipelineState;

	UINT RenderTargetDescriptorSize = 0;
	UINT CurrentFrameIndex = 0;
	HANDLE FenceEvent = nullptr;
	ComPtr<ID3D12Fence1> Fence;
	UINT64 FenceValue = 0;

	void WaitForPreviousFrame()
	{
		CommandQueue->Signal(Fence.Get(), ++FenceValue);
		if (Fence->GetCompletedValue() < FenceValue)
		{
			Fence->SetEventOnCompletion(FenceValue, FenceEvent);
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
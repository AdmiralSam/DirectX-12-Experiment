#include <Windows.h>

#include "Application.h"

LRESULT CALLBACK ProcessMessage(HWND Window, UINT Message, WPARAM WParameter, LPARAM LParameter)
{
	auto App = reinterpret_cast<Application*>(GetWindowLongPtr(Window, GWLP_USERDATA));
	switch (Message)
	{
	case WM_CREATE:
		{
			auto CreateStruct = reinterpret_cast<LPCREATESTRUCT>(LParameter);
			SetWindowLongPtr(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(CreateStruct->lpCreateParams));
		}
		return 0;

	case WM_KEYDOWN:
		if (App != nullptr)
		{
			App->OnKeyPressed(static_cast<UINT8>(WParameter));
		}
		return 0;

	case WM_KEYUP:
		if (App != nullptr)
		{
			App->OnKeyReleased(static_cast<UINT8>(WParameter));
		}
		return 0;

	case WM_PAINT:
		if (App != nullptr)
		{
			App->Update();
			App->Render();
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProc(Window, Message, WParameter, LParameter);
	}
}

int WINAPI WinMain(HINSTANCE Instance, HINSTANCE, LPSTR, int ShowCommands)
{
	auto App = new Application();

	int NumberOfArguments;
	LPWSTR* Arguments = CommandLineToArgvW(GetCommandLineW(), &NumberOfArguments);
	App->ParseCommandLineArguments(Arguments, NumberOfArguments);
	LocalFree(Arguments);

	WNDCLASSEX WindowClass = {};
	WindowClass.cbSize = sizeof(WNDCLASSEX);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = ProcessMessage;
	WindowClass.hInstance = Instance;
	WindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	WindowClass.lpszClassName = L"DirectX 12 Window Class";
	RegisterClassEx(&WindowClass);

	RECT WindowRectangle = { 0, 0, static_cast<LONG>(App->GetWidth()), static_cast<LONG>(App->GetHeight()) };
	AdjustWindowRect(&WindowRectangle, WS_OVERLAPPEDWINDOW, FALSE);

	HWND Window = CreateWindow(
		WindowClass.lpszClassName,
		App->GetWindowTitle(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowRectangle.right - WindowRectangle.left,
		WindowRectangle.bottom - WindowRectangle.top,
		nullptr,
		nullptr,
		Instance,
		App
	);

	App->SetWindow(Window);
	App->Initialize();
	ShowWindow(Window, ShowCommands);

	MSG Message = {};
	while (Message.message != WM_QUIT)
	{
		if (PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}
	}

	App->Dispose();
	return static_cast<char>(Message.wParam);
}
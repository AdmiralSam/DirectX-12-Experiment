#pragma once

#include <Windows.h>
#include <string>

class Application
{
public:
	Application();
	~Application();

	void SetWindow(HWND WindowHandle);

	void ParseCommandLineArguments(WCHAR* Arguments[], int NumberOfArguments);
	void Initialize();
	void Update();
	void Render();
	void Dispose();

	void OnKeyPressed(UINT8 Key);
	void OnKeyReleased(UINT8 Key);

	const WCHAR* GetWindowTitle() const;
	UINT GetWidth() const;
	UINT GetHeight() const;

private:
	class ApplicationImplementation;
	std::unique_ptr<ApplicationImplementation> Implementation;
};
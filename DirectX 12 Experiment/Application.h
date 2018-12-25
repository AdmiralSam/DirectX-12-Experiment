#pragma once

#include <Windows.h>

class Application
{
public:
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
};
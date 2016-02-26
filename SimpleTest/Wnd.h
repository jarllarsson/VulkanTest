#pragma once
#include <windows.h>
#include <vector>

namespace Wnd
{
	struct WndEvent
	{
		enum EventType
		{
			NONE,
			QUIT,
			RESIZE
		};
		WndEvent();
		EventType m_type;
		float m_fData1, m_fData2;
		int   m_iData1, m_iData2;
	};

	bool SetupWindow(int in_width, int in_height);
	std::vector<Wnd::WndEvent>& ProcEvents(std::vector<Wnd::WndEvent>& inout_events);
	void DestroyWindow();
	bool GetPlatformWindowInfo(HWND& out_hWnd, HINSTANCE& out_hInstance);

}
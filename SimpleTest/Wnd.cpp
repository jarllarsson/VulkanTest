#include "Wnd.h"
#include "ErrorReporting.h"
#include <SDL.h>
#undef main
#include <SDL_syswm.h>

namespace Wnd
{
	SDL_Window* win;
	int width, height;
	int fullscreenwidth; int fullscreenheight;
	bool currentFullscreen;
	int fullscreenMode;

	void ConvertSDLEvent(const SDL_Event& in_event, WndEvent& inout_wndEvent);
}

Wnd::WndEvent::WndEvent()
	: m_type(NONE)
	, m_fData1()
	, m_fData2()
	, m_iData1()
	, m_iData2()
{}




bool Wnd::SetupWindow(int in_width, int in_height)
{
	// Init SDL
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		throw ProgramError(std::string("SDL_Init Error: ") + std::string(SDL_GetError()));
	}

	fullscreenMode = /*SDL_WINDOW_FULLSCREEN_DESKTOP*/ SDL_WINDOW_FULLSCREEN;

	// Init SDL window
	Uint32 windowFlags = /*SDL_WINDOW_FULLSCREEN | */SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

	fullscreenwidth = 1280; fullscreenheight = 720;
	currentFullscreen = false;

	win = SDL_CreateWindow("Vulkangrillad korv", 100, 100, in_width, in_height, windowFlags);
	SDL_SetWindowMinimumSize(win, 8, 8);
	if (win == nullptr)
	{
		std::string err = SDL_GetError();
		SDL_Quit();
		throw ProgramError(std::string("SDL_CreateWindow Error: ") + err);
	}

	return true;
}


std::vector<Wnd::WndEvent>& Wnd::ProcEvents(std::vector<Wnd::WndEvent>& inout_events)
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		WndEvent nativeEvent;
		ConvertSDLEvent(event, nativeEvent);
		inout_events.push_back(nativeEvent);
	}

	return inout_events;
}

void Wnd::DestroyWindow()
{
	if (win)
	{
		OutputDebugString("\n\nDestroying Window\n\n");
		SDL_Quit();
	}
}

bool Wnd::GetPlatformWindowInfo(HWND& out_hWnd, HINSTANCE& out_hInstance)
{
	SDL_SysWMinfo sdlInfo;
	SDL_VERSION(&sdlInfo.version); // initialize info structure with SDL version info
	if (SDL_GetWindowWMInfo(win, &sdlInfo))
	{
		out_hWnd = sdlInfo.info.win.window;
		out_hInstance = GetModuleHandle(NULL);

		return true;
	}

	// Couldn't get sdl window info needed for interops
	return false;
}



void Wnd::ConvertSDLEvent(const SDL_Event& in_event, WndEvent& inout_wndEvent)
{
	bool shouldQuit = false;

	switch (in_event.type)
	{
		case SDL_KEYDOWN:
		{
			if (in_event.key.keysym.sym == SDLK_ESCAPE)
			{
				inout_wndEvent.m_type = WndEvent::QUIT;
				break;
			}

			break;
		}

		case SDL_WINDOWEVENT:
		{
			switch (in_event.window.event)
			{
			case SDL_WINDOWEVENT_RESIZED:
			{
				inout_wndEvent.m_type = WndEvent::RESIZE;
				inout_wndEvent.m_iData1 = in_event.window.data1;
				inout_wndEvent.m_iData2 = in_event.window.data2;
				break;
			}
			}
			break;
		}

		case SDL_QUIT:
		{
			inout_wndEvent.m_type = WndEvent::QUIT;
			break;
		}
	}

}

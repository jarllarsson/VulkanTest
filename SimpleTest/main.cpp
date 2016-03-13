/*#include <vld.h>*/
//#include "Wnd.h"
#include "ErrorReporting.h"
#include "VulkanGraphics.h"

int main(int argc, char* argv[])
{
	int width = 800, height = 600;
	int x = 0;
	try
	{
		int i = 0;
		//Wnd::SetupWindow(width, height);
	}
	catch (std::exception& e)
	{
		MessageBox(0, e.what(), "Error!", MB_OK);
		return -1;
 	}

	// Init Vulkan
	HINSTANCE hInstance;
	HWND hWnd;
	//Wnd::GetPlatformWindowInfo(hWnd, hInstance);
	VulkanGraphics vulkanGraphics(hWnd, hInstance, width, height);

	bool run = true;
	while (run)
	{
		x++;
		width--;
		height *= 2;
	}
	x = 100;
	width = 10000;
	height = 0;

	// Main loop
// 	bool run = true;
// 	std::vector<Wnd::WndEvent> events;
// 	while (run)
// 	{
// 		// Events
// 		Wnd::ProcEvents(events);
// 		for (auto const &n : events)
// 		{
// 			if (n.m_type == Wnd::WndEvent::QUIT) run = false;
// 		}
// 
// 		// Main code
// 		// ========================
// 
// 
// 		// ========================
// 	}
// 
// 
// 	// Cleanup
 	//Wnd::DestroyWindow();

	return 0;
}
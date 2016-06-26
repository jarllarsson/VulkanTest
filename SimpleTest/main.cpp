/*#include <vld.h>*/
#include "ErrorReporting.h"
#include "Wnd.h"
#include "VulkanGraphics.h"

int main(int argc, char* argv[])
{
	int width = 800, height = 600;
	std::unique_ptr<VulkanGraphics> vulkanGraphics = nullptr;

	try 
	{
		Wnd::SetupWindow(width, height);

		// Init Vulkan
		HINSTANCE hInstance;
		HWND hWnd;
		Wnd::GetPlatformWindowInfo(hWnd, hInstance);
		vulkanGraphics = std::make_unique<VulkanGraphics>(hWnd, hInstance, width, height);
	}
	catch (ProgramError& e)
	{
		MessageBox(0, e.what(), "Error!", MB_OK);
		return -1;
	}
	if (vulkanGraphics == nullptr)
	{
		MessageBox(0, "Graphics not initialized", "Error!", MB_OK);
		return -1;
	}

	// Main loop
	bool run = true;
	std::vector<Wnd::WndEvent> events;
	while (run)
	{
		// Events
		Wnd::ProcEvents(events);
		for (auto const &n : events)
		{
			if (n.m_type == Wnd::WndEvent::QUIT) run = false;
		}

		// Main code
		// ========================

		vulkanGraphics->Render();

		// ========================
	}


	// Cleanup
	Wnd::DestroyWindow();

	return 0;
}
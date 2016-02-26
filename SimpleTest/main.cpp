/*#include <vld.h>*/
#include "ErrorReporting.h"
#include "Wnd.h"



int main(int argc, char* argv[])
{
	try 
	{
		Wnd::SetupWindow(800, 600);
	}
	catch (ProgramError& e)
	{
		MessageBox(0, e.what(), "Error!", MB_OK);
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


		// ========================
	}


	// Cleanup
	Wnd::DestroyWindow();

	return 0;
}
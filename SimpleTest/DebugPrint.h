#pragma once

#include <Windows.h>
/*#include "ConsoleContext.h"*/
// =======================================================================================
//                                      DebugPrint
// =======================================================================================

///---------------------------------------------------------------------------------------
/// \brief	Brief
///        
/// # DebugPrint
/// 
/// 17-4-2013 Jarl Larsson
///---------------------------------------------------------------------------------------

/***************************************************************************/
/* FORCE_DISABLE_OUTPUT removes all debug prints silently discarding them  */
/***************************************************************************/
// #define FORCE_DISABLE_OUTPUT


// Will only print in debug, will replace call in release with "nothing"
// call like this: DEBUGPRINT(("text"));
//  std::cout << __FILE__ << " " << __LINE__ << " " << (x) maybe?
// #ifdef _DEBUG
static void debugPrint(const std::string& in_str);
#ifndef FORCE_DISABLE_OUTPUT
#define DEBUGPRINT(x) debugPrint(x + std::string("\n"))
#else
#define DEBUGPRINT(x)
#endif
void debugPrint(const std::string& in_str)
{
	OutputDebugStringA(in_str.c_str());
	// ConsoleContext::addMsg(string(msg), false); not used in this app
}


// Warning version
// #ifdef _DEBUG
static void debugWarn(const std::string& in_str);
#ifndef FORCE_DISABLE_OUTPUT
#define DEBUGWARNING(x) debugWarn x
#else
#define DEBUGWARNING(x)
#endif
void debugWarn(const std::string& in_str)
{
	OutputDebugStringA((in_str.c_str()));
	MessageBoxA(NULL, (in_str.c_str()), "Warning!", MB_ICONWARNING);
}
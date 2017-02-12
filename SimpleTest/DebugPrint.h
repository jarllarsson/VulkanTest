#pragma once

#include <Windows.h>
#include <string>
#include <iostream>
#include <sstream>
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
/// 12-2-2017 Simplified, changed to stringstream
///---------------------------------------------------------------------------------------

/***************************************************************************/
/* FORCE_DISABLE_OUTPUT removes all debug prints silently discarding them  */
/***************************************************************************/
// #define FORCE_DISABLE_OUTPUT


// Will only print in debug, will replace call in release with "nothing"
// Uses do while to expand into a regular statement instead of a compound statement
// allowing for the following:
// if(foo)
//   LOG(x);
// else
//   bar(x);

#ifndef FORCE_DISABLE_OUTPUT
#define LOG(x) \
do { \
std::ostringstream _o_ss_dbg; \
_o_ss_dbg << "LOG: " << __FILE__ << " ln: " << __LINE__ << " " << x << "\n"; \
OutputDebugString(_o_ss_dbg.str().c_str()); \
std::cout << _o_ss_dbg.str(); \
} while (0)

#else
#define LOG(x)
#endif
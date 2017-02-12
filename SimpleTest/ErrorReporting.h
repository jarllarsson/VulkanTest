#pragma once

#include <exception>
#include <string>
#include <assert.h>
#include <iostream>
#include <sstream>

struct ProgramError : std::exception
{
	ProgramError(const std::ostringstream& in_errorMessage) : m_errorMsg(in_errorMessage.str()) { 
	}
	virtual ~ProgramError() throw() {};

	virtual const char* what() const throw() { return m_errorMsg.c_str(); }

	std::string m_errorMsg;
};

// In debug build, assert false, in release throw exception
#ifdef _DEBUG

#define ERROR_IF(x, msg) \
do { \
if (x) \
{ \
	std::ostringstream _o_ss_err; \
	_o_ss_err << "ERROR: " << __FILE__ << " ln: " << __LINE__ << " " << msg << "\n"; \
	OutputDebugString(_o_ss_err.str().c_str()); \
	std::cout << _o_ss_err.str(); \
	assert(false); \
} \
} while (0)

#else

#define ERROR_IF(x, msg) \
do { \
if (x) \
{ \
	std::ostringstream _o_ss_err; \
	_o_ss_err << "ERROR: " << msg << "\n"; \
	OutputDebugString(_o_ss_err.str().c_str()); \
	std::cout << _o_ss_err.str(); \
	throw ProgramError(_o_ss_err); \
} \
} while (0)

#endif



#define ERROR_ALWAYS(msg) ERROR_IF(true, msg)
#pragma once

#include <exception>
#include <string>
#include <assert.h>

struct ProgramError : std::exception
{
	ProgramError(const std::string& in_errorMessage) : m_errorMsg("When: " + in_errorMessage) { 
#ifdef _DEBUG
		assert(false);
#endif // _DEBUG
	}
	virtual ~ProgramError() throw() {};

	virtual const char* what() const throw() { return m_errorMsg.c_str(); }

	std::string m_errorMsg;
};

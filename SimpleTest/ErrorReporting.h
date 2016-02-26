#pragma once

#include <exception>
#include <string>

struct ProgramError : std::exception
{
	ProgramError(const std::string& in_errorMessage) : m_errorMsg(in_errorMessage) {}
	virtual ~ProgramError() throw() {};

	virtual const char* what() const throw() { return m_errorMsg.c_str(); }

	std::string m_errorMsg;
};

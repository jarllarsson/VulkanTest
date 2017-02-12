#pragma once
#include <functional>
#include "DebugPrint.h"

/*!
* \class VkPtr
*
* \brief
*
* An auto deleter class for handling Vulkan objects (or other).
* Based on auto-deleter as seen on https://vulkan-tutorial.com
*
* \author Jarl
* \date 2016-2017
*/

template <typename T>
class VkObj
{
public:
	// Constructor variants based on the three variants of creating Vulkan objects
	VkObj() : VkObj([](T nullObj) {}) // dummy empty function
	: m_obj(VK_NULL_HANDLE)
	{}

	// Currently not implemented support for VkAllocationCallbacks
	// nullptr hardcoded for those atm

	// Creation with only object
	VkObj(std::function<void(T, VkAllocationCallbacks*)> in_deleterFunc, T in_init = VK_NULL_HANDLE)
	: m_obj(in_init)
	{
		Init(in_deleterFunc);
	}

	VkObj(const VkObj<VkInstance>& in_instance,
		std::function<void(VkInstance, T, VkAllocationCallbacks*)> in_deleterFunc, T in_init = VK_NULL_HANDLE)
	: m_obj(in_init)
	{
		Init(in_instance, in_deleterFunc);
	}


	VkObj(const VkObj<VkDevice>& in_device,
		std::function<void(VkDevice, T, VkAllocationCallbacks*)> in_deleterFunc, T in_init = VK_NULL_HANDLE)
	: m_obj(in_init)
	{
		Init(in_device, in_deleterFunc);
	}

#ifdef _DEBUG
	// Special constructors that also stores a debug name
	VkObj(const VkObj<VkInstance>& in_instance,
		std::function<void(VkInstance, T, VkAllocationCallbacks*)> in_deleterFunc, std::string& in_dbgName, T in_init = VK_NULL_HANDLE)
		: m_obj(in_init)
	{
		Init(in_instance, in_deleterFunc);
		m_dbgName = in_dbgName;
	}

	VkObj(const VkObj<VkDevice>& in_device,
		std::function<void(VkDevice, T, VkAllocationCallbacks*)> in_deleterFunc, std::string& in_dbgName, T in_init = VK_NULL_HANDLE)
		: m_obj(in_init)
	{
		Init(in_device, in_deleterFunc);
		m_dbgName = in_dbgName;
	}

	void SetDbgName(std::string& in_dbgName)
	{
		m_dbgName = in_dbgName;
	}
#endif // _DEBUG


	~VkObj()
	{
#ifdef _DEBUG
		if (!m_dbgName.empty())
			LOG("Vulkan Object: Removing: " << m_dbgName);
		else
			LOG("Vulkan Object: Removing: (unnamed)");
#endif // _DEBUG
		Clean();
	}

	T Get()
	{
		return m_obj;
	}

	bool operator ! () const
	{
		return m_obj == VK_NULL_HANDLE;
	}

	const T* operator &() const 
	{
		return &m_obj;
	}

	T* Replace() 
	{
#ifdef _DEBUG
		if (!m_dbgName.empty())
			LOG("Vulkan Object: Replacing: " << m_dbgName);
		else
			LOG("Vulkan Object: Replacing: (unnamed)");
#endif // _DEBUG
		Clean();
		return &m_obj;
	}

	void Reset(T* in_init)
	{
		T* ptr = Replace();
		ptr = in_init;
	}

	operator T () const 
	{
		return m_obj;
	}

	void operator = (T rhs) 
	{
		if (rhs != m_obj)
		{
			Clean();
			m_obj = rhs;
		}
	}

	template<typename V>
	bool operator == (V rhs) 
	{
		return m_obj == T(rhs);
	}


private:
	void Init(std::function<void(T, VkAllocationCallbacks*)> in_deleterFunc)
	{
		// Assign a lambda to the deleter functor
		// that calls the in_deleterFunc functor (capture by value in capture clause [] )
		// with parameter T obj (is set as parameter when we call m_deleter)
		m_deleter =
			[in_deleterFunc](T obj) {
			in_deleterFunc(obj, nullptr);
		}; // internal delete is call 
	}

	void Init(const VkObj<VkInstance>& in_instance,
		std::function<void(VkInstance, T, VkAllocationCallbacks*)> in_deleterFunc)
	{
		// Assign lambda, here also bind in_instance as ref
		m_deleter =
			[&in_instance, in_deleterFunc](T obj)
		{
			in_deleterFunc(in_instance, obj, nullptr);
		};
	}


	void Init(const VkObj<VkDevice>& in_device,
		std::function<void(VkDevice, T, VkAllocationCallbacks*)> in_deleterFunc)
	{
		// Assign lambda, here also bind in_device as ref
		m_deleter =
			[&in_device, in_deleterFunc](T obj)
		{
			in_deleterFunc(in_device, obj, nullptr);
		};
	}

	void Clean()
	{
		if (m_obj != VK_NULL_HANDLE) m_deleter(m_obj);
		m_obj = VK_NULL_HANDLE;
	}

	T m_obj{ VK_NULL_HANDLE };
	std::function<void(T)> m_deleter;

#ifdef _DEBUG
	std::string m_dbgName;
#endif // _DEBUG

};

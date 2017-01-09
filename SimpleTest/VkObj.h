#pragma once
#include <functional>

/*!
* \class VkPtr
*
* \brief
*
* An auto deleter class for handling Vulkan objects.
* Based on auto-deleter as seen on https://vulkan-tutorial.com
*
* TODO: Reference counting
*
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
	VkObj(std::function<void(T, VkAllocationCallbacks*)> in_deleterFunc)
	: m_obj(VK_NULL_HANDLE)
	{
		// Assign a lambda to the deleter functor
		// that calls the in_deleterFunc functor (capture by value in capture clause [] )
		// with parameter T obj (is set as parameter when we call m_deleter)
		m_deleter = 
			[in_deleterFunc](T obj) {
				in_deleterFunc(obj, nullptr); 
			}; // internal delete is call 
	}

	VkObj(const VkObj<VkInstance>& in_instance,
		std::function<void(VkInstance, T, VkAllocationCallbacks*)> in_deleterFunc)
	: m_obj(VK_NULL_HANDLE)
	{
		// Assign lambda, here also bind in_instance as ref
		m_deleter = 
			[&in_instance, in_deleterFunc](T obj) 
			{
				in_deleterFunc(in_instance, obj, nullptr);
			};
	}


	VkObj(const VkObj<VkDevice>& in_device,
		std::function<void(VkDevice, T, VkAllocationCallbacks*)> in_deleterFunc)
	: m_obj(VK_NULL_HANDLE)
	{
		// Assign lambda, here also bind in_device as ref
		m_deleter = 
			[&in_device, in_deleterFunc](T obj)
			{ 
				in_deleterFunc(in_device, obj, nullptr); 
			};
	}

	~VkObj()
	{
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
		Clean();
		return &m_obj;
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
	void Clean()
	{
		if (m_obj != VK_NULL_HANDLE) m_deleter(m_obj);
		m_obj = VK_NULL_HANDLE;
	}

	T m_obj{ VK_NULL_HANDLE };
	std::function<void(T)> m_deleter;
};

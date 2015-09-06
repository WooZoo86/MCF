// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2015, LH_Mouse. All wrongs reserved.

#ifndef MCF_THREAD_THREAD_LOCAL_HPP_
#define MCF_THREAD_THREAD_LOCAL_HPP_

#include "../../MCFCRT/env/thread.h"
#include "../Utilities/Noncopyable.hpp"
#include "../Utilities/Assert.hpp"
#include "../Core/UniqueHandle.hpp"
#include "../Core/Exception.hpp"
#include <type_traits>
#include <cstddef>
#include <cstdint>

namespace MCF {

namespace Impl_ThreadLocal {
	struct TlsKeyDeleter {
		constexpr void *operator()() const noexcept {
			return nullptr;
		}
		void operator()(void *pKey) const noexcept {
			::MCF_CRT_TlsFreeKey(pKey);
		}
	};

	template<class ElementT>
	class ThreadLocalTrivialSmallEnough : NONCOPYABLE {
		static_assert(std::is_trivial<ElementT>::value && !std::is_array<ElementT>::value && (sizeof(ElementT) <= sizeof(std::intptr_t)), "!");

	private:
		const ElementT x_vDefault;
		UniqueHandle<TlsKeyDeleter> x_pTlsKey;

	public:
		explicit ThreadLocalTrivialSmallEnough(ElementT vDefault = ElementT())
			: x_vDefault(std::move(vDefault))
		{
			if(!x_pTlsKey.Reset(::MCF_CRT_TlsAllocKey(nullptr))){
				DEBUG_THROW(SystemError, "MCF_CRT_TlsAllocKey");
			}
		}

	public:
		ElementT Get() const noexcept {
			bool bHasValue;
			std::intptr_t nValue;
			if(!::MCF_CRT_TlsGet(x_pTlsKey.Get(), &bHasValue, &nValue)){
				ASSERT_MSG(false, L"MCF_CRT_TlsGet() 失败。");
			}
			if(bHasValue){
				ElementT vElement;
				std::memcpy(&vElement, &nValue, sizeof(vElement));
				return vElement;
			}
			return x_vDefault;
		}
		void Set(const ElementT &vElement){
			std::intptr_t nValue = 0;
			std::memcpy(&nValue, &vElement, sizeof(vElement));
			if(!::MCF_CRT_TlsReset(x_pTlsKey.Get(), nValue)){ // noexcept
				DEBUG_THROW(SystemError, "MCF_CRT_TlsReset");
			}
		}
	};

	template<class ElementT>
	class ThreadLocalGeneric : NONCOPYABLE {
	private:
		const ElementT x_vDefault;
		UniqueHandle<TlsKeyDeleter> x_pTlsKey;

	public:
		explicit ThreadLocalGeneric(ElementT vDefault = ElementT())
			: x_vDefault(std::move(vDefault))
		{
			if(!x_pTlsKey.Reset(::MCF_CRT_TlsAllocKey([](std::intptr_t nValue){ delete reinterpret_cast<ElementT *>(nValue); }))){
				DEBUG_THROW(SystemError, "MCF_CRT_TlsAllocKey");
			}
		}

	public:
		const ElementT &Get() const noexcept {
			bool bHasValue;
			std::intptr_t nValue;
			if(!::MCF_CRT_TlsGet(x_pTlsKey.Get(), &bHasValue, &nValue)){
				ASSERT_MSG(false, L"MCF_CRT_TlsGet() 失败。");
			}
			if(bHasValue){
				const auto pElement = reinterpret_cast<const ElementT *>(nValue);
				if(pElement){
					return *pElement;
				}
			}
			return x_vDefault;
		}
		void Set(ElementT vElement){
			const auto pElement = new ElementT(std::move(vElement));
			if(!::MCF_CRT_TlsReset(x_pTlsKey.Get(), reinterpret_cast<std::intptr_t>(pElement))){ // noexcept
				delete pElement;
				DEBUG_THROW(SystemError, "MCF_CRT_TlsReset");
			}
		}
	};
}

template<class ElementT>
using ThreadLocal =
	std::conditional_t<std::is_trivial<ElementT>::value && !std::is_array<ElementT>::value && (sizeof(ElementT) <= sizeof(std::intptr_t)),
		Impl_ThreadLocal::ThreadLocalTrivialSmallEnough<ElementT>,
		Impl_ThreadLocal::ThreadLocalGeneric<ElementT>
		>;

}

#endif

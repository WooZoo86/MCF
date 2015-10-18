// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2015, LH_Mouse. All wrongs reserved.

#ifndef MCF_THREAD_KERNEL_EVENT_HPP_
#define MCF_THREAD_KERNEL_EVENT_HPP_

#include "../Utilities/Noncopyable.hpp"
#include "../Core/StringView.hpp"
#include "../Core/_UniqueNtHandle.hpp"
#include <cstddef>
#include <cstdint>

namespace MCF {

class KernelEvent : NONCOPYABLE {
private:
	Impl_UniqueNtHandle::UniqueNtHandle x_hEvent;

public:
	explicit KernelEvent(bool bInitSet);
	KernelEvent(bool bInitSet, const WideStringView &wsvName, bool bFailIfExists);

public:
	bool Wait(std::uint64_t u64UntilFastMonoClock) const noexcept;
	void Wait() const noexcept;
	bool IsSet() const noexcept;
	bool Set() noexcept;
	bool Reset() noexcept;
};

}

#endif

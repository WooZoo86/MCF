// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2015, LH_Mouse. All wrongs reserved.

#ifndef MCF_THREAD_KERNEL_MUTEX_HPP_
#define MCF_THREAD_KERNEL_MUTEX_HPP_

#include "../Utilities/Noncopyable.hpp"
#include "../Core/StringView.hpp"
#include "../Core/_UniqueNtHandle.hpp"
#include "_UniqueLockTemplate.hpp"
#include <cstdint>

namespace MCF {

class KernelMutex : NONCOPYABLE {
public:
	using UniqueLock = Impl_UniqueLockTemplate::UniqueLockTemplate<KernelMutex>;

private:
	Impl_UniqueNtHandle::UniqueNtHandle x_hEvent;

public:
	KernelMutex();
	KernelMutex(const WideStringView &wsvName, bool bFailIfExists);

public:
	bool Try(std::uint64_t u64UntilFastMonoClock = 0) noexcept;
	void Lock() noexcept;
	void Unlock() noexcept;

	UniqueLock TryGetLock(std::uint64_t u64UntilFastMonoClock = 0) noexcept {
		UniqueLock vLock(*this, false);
		vLock.Try(u64UntilFastMonoClock);
		return vLock;
	}
	UniqueLock GetLock() noexcept {
		return UniqueLock(*this);
	}
};

namespace Impl_UniqueLockTemplate {
	template<>
	inline bool KernelMutex::UniqueLock::X_DoTry(std::uint64_t u64UntilFastMonoClock) const noexcept {
		return x_pOwner->Try(u64UntilFastMonoClock);
	}
	template<>
	inline void KernelMutex::UniqueLock::X_DoLock() const noexcept {
		x_pOwner->Lock();
	}
	template<>
	inline void KernelMutex::UniqueLock::X_DoUnlock() const noexcept {
		x_pOwner->Unlock();
	}
}

}

#endif

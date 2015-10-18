// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2015, LH_Mouse. All wrongs reserved.

#include "../StdMCF.hpp"
#include "KernelRecursiveMutex.hpp"
#include "../Core/Exception.hpp"
#include "../Core/Time.hpp"
#include <winternl.h>
#include <ntdef.h>

extern "C" __attribute__((__dllimport__, __stdcall__))
NTSTATUS NtCreateMutant(HANDLE *pHandle, ACCESS_MASK dwDesiredAccess, const OBJECT_ATTRIBUTES *pObjectAttributes, BOOLEAN bInitialOwner) noexcept;

extern "C" __attribute__((__dllimport__, __stdcall__))
NTSTATUS NtReleaseMutant(HANDLE hMutant, LONG *plPrevCount) noexcept;

namespace MCF {

namespace {
	Impl_UniqueNtHandle::UniqueNtHandle CreateMutexHandle(const WideStringView &wsvName, bool bFailIfExists){
		const auto uSize = wsvName.GetSize() * sizeof(wchar_t);
		if(uSize > UINT16_MAX){
			DEBUG_THROW(SystemError, ERROR_INVALID_PARAMETER, "The name for a kernel mutex is too long"_rcs);
		}
		::UNICODE_STRING ustrObjectName;
		ustrObjectName.Length        = uSize;
		ustrObjectName.MaximumLength = uSize;
		ustrObjectName.Buffer        = (PWSTR)wsvName.GetBegin();
		::OBJECT_ATTRIBUTES vObjectAttributes;
		InitializeObjectAttributes(&vObjectAttributes, &ustrObjectName, bFailIfExists ? 0 : OBJ_OPENIF, nullptr, nullptr);

		HANDLE hMutex;
		const auto lStatus = ::NtCreateMutant(&hMutex, MUTANT_ALL_ACCESS, &vObjectAttributes, false);
		if(!NT_SUCCESS(lStatus)){
			DEBUG_THROW(SystemError, ::RtlNtStatusToDosError(lStatus), "NtCreateMutant"_rcs);
		}
		return Impl_UniqueNtHandle::UniqueNtHandle(hMutex);
	}
}

// 构造函数和析构函数。
KernelRecursiveMutex::KernelRecursiveMutex()
	: x_hMutex(CreateMutexHandle(nullptr, false))
{
}
KernelRecursiveMutex::KernelRecursiveMutex(const WideStringView &wsvName, bool bFailIfExists)
	: x_hMutex(CreateMutexHandle(wsvName, bFailIfExists))
{
}

// 其他非静态成员函数。
bool KernelRecursiveMutex::Try(std::uint64_t u64UntilFastMonoClock) noexcept {
	::LARGE_INTEGER liTimeout;
	const auto u64Now = GetFastMonoClock();
	if(u64Now >= u64UntilFastMonoClock){
		liTimeout.QuadPart = 0;
	} else {
		const auto u64DeltaMillisec = u64UntilFastMonoClock - u64Now;
		const auto n64Delta100Nanosec = static_cast<std::int64_t>(u64DeltaMillisec * 10000);
		if(static_cast<std::uint64_t>(n64Delta100Nanosec / 10000) != u64DeltaMillisec){
			liTimeout.QuadPart = INT64_MIN;
		} else {
			liTimeout.QuadPart = -n64Delta100Nanosec;
		}
	}
	const auto lStatus = ::NtWaitForSingleObject(x_hMutex.Get(), false, &liTimeout);
	if(!NT_SUCCESS(lStatus)){
		ASSERT_MSG(false, L"NtWaitForSingleObject() 失败。");
	}
	return lStatus != STATUS_TIMEOUT;
}
void KernelRecursiveMutex::Lock() noexcept {
	const auto lStatus = ::NtWaitForSingleObject(x_hMutex.Get(), false, nullptr);
	if(!NT_SUCCESS(lStatus)){
		ASSERT_MSG(false, L"NtWaitForSingleObject() 失败。");
	}
}
void KernelRecursiveMutex::Unlock() noexcept {
	LONG lPrevCount;
	const auto lStatus = ::NtReleaseMutant(x_hMutex.Get(), &lPrevCount);
	if(!NT_SUCCESS(lStatus)){
		ASSERT_MSG(false, L"NtReleaseMutant() 失败。");
	}
}

}

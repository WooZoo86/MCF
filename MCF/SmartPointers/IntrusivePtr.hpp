// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2016, LH_Mouse. All wrongs reserved.

#ifndef MCF_SMART_POINTERS_INTRUSIVE_PTR_HPP_
#define MCF_SMART_POINTERS_INTRUSIVE_PTR_HPP_

#include "../Utilities/Assert.hpp"
#include "../Utilities/Bail.hpp"
#include "../Utilities/DeclVal.hpp"
#include "../Function/Comparators.hpp"
#include "../Thread/Mutex.hpp"
#include "../Thread/Atomic.hpp"
#include "DefaultDeleter.hpp"
#include "UniquePtr.hpp"
#include <utility>
#include <type_traits>
#include <cstddef>

namespace MCF {

template<typename ObjectT, class DeleterT = DefaultDeleter<ObjectT>>
class IntrusiveBase;

template<typename ObjectT, class DeleterT = DefaultDeleter<ObjectT>>
class IntrusivePtr;
template<typename ObjectT, class DeleterT = DefaultDeleter<ObjectT>>
class IntrusiveWeakPtr;

namespace Impl_IntrusivePtr {
	template<class DeleterT>
	class DeletableBase;

	class RefCountBase {
		template<class DeleterT>
		friend class DeletableBase;

	private:
		mutable Atomic<std::size_t> x_uRef;

	protected:
		constexpr RefCountBase() noexcept
			: x_uRef(1)
		{
		}
		RefCountBase(const RefCountBase &) noexcept
			: RefCountBase() // 默认构造。
		{
		}
		RefCountBase &operator=(const RefCountBase &) noexcept {
			return *this; // 无操作。
		}
		~RefCountBase(){
			ASSERT_MSG(x_uRef.Load(kAtomicRelaxed) <= 1, L"析构正在共享的被引用计数管理的对象。");
		}

	public:
		bool IsUnique() const volatile noexcept {
			return GetRef() == 1;
		}
		std::size_t GetRef() const volatile noexcept {
			return x_uRef.Load(kAtomicRelaxed);
		}
		bool TryAddRef() const volatile noexcept {
			ASSERT((std::ptrdiff_t)x_uRef.Load(kAtomicRelaxed) >= 0);

			auto uOldRef = x_uRef.Load(kAtomicRelaxed);
			for(;;){
				if(uOldRef == 0){
					return false;
				}
				if(x_uRef.CompareExchange(uOldRef, uOldRef + 1, kAtomicRelaxed)){
					return true;
				}
			}
		}
		void AddRef() const volatile noexcept {
			ASSERT((std::ptrdiff_t)x_uRef.Load(kAtomicRelaxed) > 0);

			x_uRef.Increment(kAtomicRelaxed);
		}
		bool DropRef() const volatile noexcept {
			ASSERT((std::ptrdiff_t)x_uRef.Load(kAtomicRelaxed) > 0);

			return x_uRef.Decrement(kAtomicRelaxed) == 0;
		}
	};

	template<typename DstT, typename SrcT, typename = DstT>
	struct StaticCastOrDynamicCastHelper {
		DstT operator()(SrcT &&vSrc) const {
			return dynamic_cast<DstT>(std::forward<SrcT>(vSrc));
		}
	};
	template<typename DstT, typename SrcT>
	struct StaticCastOrDynamicCastHelper<DstT, SrcT,
		decltype(static_cast<DstT>(DeclVal<SrcT>()))>
	{
		constexpr DstT operator()(SrcT &&vSrc) const noexcept {
			return static_cast<DstT>(std::forward<SrcT>(vSrc));
		}
	};

	template<typename DstT, typename SrcT>
	DstT StaticCastOrDynamicCast(SrcT &&vSrc){
		return StaticCastOrDynamicCastHelper<DstT, SrcT>()(std::forward<SrcT>(vSrc));
	}

	struct ViewPoolElement final {
		static void *operator new(std::size_t uSize);
		static void operator delete(void *pRaw) noexcept;

		ViewPoolElement *pNext;
		void *apPadding[3];
	};

	template<typename OwnerT, class DeleterT>
	class WeakViewTemplate final : public RefCountBase  {
	public:
		static void *operator new(std::size_t uSize){
			static_assert(sizeof(WeakViewTemplate) <= sizeof(ViewPoolElement), "Please update the definition ViewPoolElement to make sure it is large enough to hold this WeakViewTemplate.");

			ASSERT(uSize == sizeof(WeakViewTemplate));

			return ViewPoolElement::operator new(uSize);
		}
		static void operator delete(void *pRaw) noexcept {
			if(!pRaw){
				return;
			}

			ViewPoolElement::operator delete(pRaw);
		}

	private:
		mutable Mutex x_mtxGuard;
		OwnerT *x_pOwner;

	public:
		explicit constexpr WeakViewTemplate(OwnerT *pOwner) noexcept
			: x_mtxGuard(), x_pOwner(pOwner)
		{
		}

	public:
		bool IsOwnerAlive() const noexcept {
			const Mutex::UniqueLock vLock(x_mtxGuard);
			const auto pOwner = x_pOwner;
			if(!pOwner){
				return false;
			}
			if(static_cast<const volatile RefCountBase *>(pOwner)->GetRef() == 0){
				return false;
			}
			return true;
		}
		void ClearOwner() noexcept {
			const Mutex::UniqueLock vLock(x_mtxGuard);
			x_pOwner = nullptr;
		}

		template<typename OtherT>
		IntrusivePtr<OtherT, DeleterT> LockOwner() const noexcept {
			const Mutex::UniqueLock vLock(x_mtxGuard);
			const auto pOther = StaticCastOrDynamicCast<OtherT *>(x_pOwner);
			if(!pOther){
				return nullptr;
			}
			if(!static_cast<const volatile RefCountBase *>(pOther)->TryAddRef()){
				return nullptr;
			}
			return IntrusivePtr<OtherT, DeleterT>(pOther);
		}
	};
	template<class DeleterT>
	class DeletableBase : public RefCountBase {
		template<typename, class>
		friend class IntrusivePtr;
		template<typename, class>
		friend class IntrusiveWeakPtr;

	private:
		using X_WeakView = WeakViewTemplate<DeletableBase, DeleterT>;

	private:
		mutable Atomic<X_WeakView *> x_pView;

	public:
		constexpr DeletableBase() noexcept
			: x_pView(nullptr)
		{
		}
		constexpr DeletableBase(const DeletableBase &) noexcept
			: DeletableBase()
		{
		}
		DeletableBase &operator=(const DeletableBase &) noexcept {
			return *this;
		}
		virtual ~DeletableBase(){
			const auto pView = x_pView.Load(kAtomicConsume);
			if(pView){
				if(static_cast<const volatile RefCountBase *>(pView)->DropRef()){
					delete pView;
				} else {
					pView->ClearOwner();
				}
			}
		}

	private:
		X_WeakView *X_CreateView() const volatile {
			auto pView = x_pView.Load(kAtomicConsume);
			if(!pView){
				const auto pNewView = new X_WeakView(const_cast<DeletableBase *>(this));
				if(x_pView.CompareExchange(pView, pNewView, kAtomicAcqRel, kAtomicConsume)){
					pView = pNewView;
				} else {
					delete pNewView;
				}
			}
			pView->AddRef();
			return pView;
		}
	};
}

template<typename ObjectT, class DeleterT>
class IntrusiveBase : public Impl_IntrusivePtr::DeletableBase<DeleterT> {
	static_assert(!std::is_array<ObjectT>::value, "IntrusiveBase doesn't accept arrays.");

protected:
	template<typename CvOtherT, typename CvThisT>
	static IntrusivePtr<CvOtherT, DeleterT> X_ForkShared(CvThisT *pThis) noexcept {
		const auto pOther = Impl_IntrusivePtr::StaticCastOrDynamicCast<CvOtherT *>(pThis);
		if(!pOther){
			return nullptr;
		}
		static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pOther)->AddRef();
		return IntrusivePtr<CvOtherT, DeleterT>(pOther);
	}
	template<typename CvOtherT, typename CvThisT>
	static IntrusiveWeakPtr<CvOtherT, DeleterT> X_ForkWeak(CvThisT *pThis){
		const auto pOther = Impl_IntrusivePtr::StaticCastOrDynamicCast<CvOtherT *>(pThis);
		if(!pOther){
			return nullptr;
		}
		return IntrusiveWeakPtr<CvOtherT, DeleterT>(pOther);
	}

public:
	template<typename OtherT = ObjectT>
	IntrusivePtr<const volatile OtherT, DeleterT> Share() const volatile noexcept {
		return X_ForkShared<const volatile OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusivePtr<const OtherT, DeleterT> Share() const noexcept {
		return X_ForkShared<const OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusivePtr<volatile OtherT, DeleterT> Share() volatile noexcept {
		return X_ForkShared<volatile OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusivePtr<OtherT, DeleterT> Share() noexcept {
		return X_ForkShared<OtherT>(this);
	}

	template<typename OtherT = ObjectT>
	IntrusiveWeakPtr<const volatile OtherT, DeleterT> Weaken() const volatile {
		return X_ForkWeak<const volatile OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusiveWeakPtr<const OtherT, DeleterT> Weaken() const {
		return X_ForkWeak<const OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusiveWeakPtr<volatile OtherT, DeleterT> Weaken() volatile {
		return X_ForkWeak<volatile OtherT>(this);
	}
	template<typename OtherT = ObjectT>
	IntrusiveWeakPtr<OtherT, DeleterT> Weaken(){
		return X_ForkWeak<OtherT>(this);
	}
};

template<typename ObjectT, class DeleterT>
class IntrusivePtr {
	static_assert(sizeof(dynamic_cast<const volatile IntrusiveBase<ObjectT, DeleterT> *>(DeclVal<ObjectT *>())), "Unable to locate IntrusiveBase for the managed object type.");

	template<typename, class>
	friend class IntrusivePtr;
	template<typename, class>
	friend class WeakIntrusivePtr;

public:
	using Element = ObjectT;
	using Deleter = DeleterT;

	static_assert(noexcept(Deleter()(DeclVal<std::remove_cv_t<Element> *>())), "Deleter must not throw.");

public:
	static const IntrusivePtr kNull;

private:
	Element *x_pElement;

private:
	Element *X_Fork() noexcept {
		const auto pElement = x_pElement;
		if(pElement){
			static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pElement)->AddRef();
		}
		return pElement;
	}
	void X_Dispose() noexcept {
		const auto pElement = x_pElement;
		if(pElement){
			if(static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pElement)->DropRef()){
				Deleter()(const_cast<std::remove_cv_t<Element> *>(pElement));
			}
		}
#ifndef NDEBUG
		x_pElement = (Element *)(std::uintptr_t)0xDEADBEEFDEADBEEF;
#endif
	}

public:
	constexpr IntrusivePtr(std::nullptr_t = nullptr) noexcept
		: x_pElement(nullptr)
	{
	}
	explicit IntrusivePtr(Element *rhs) noexcept
		: x_pElement(rhs)
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename UniquePtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename UniquePtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusivePtr(UniquePtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept
		: x_pElement(rhs.Release())
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusivePtr(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) noexcept
		: x_pElement(rhs.X_Fork())
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusivePtr(IntrusivePtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept
		: x_pElement(rhs.Release())
	{
	}
	IntrusivePtr(const IntrusivePtr &rhs) noexcept
		: x_pElement(rhs.X_Fork())
	{
	}
	IntrusivePtr(IntrusivePtr &&rhs) noexcept
		: x_pElement(rhs.Release())
	{
	}
	IntrusivePtr &operator=(const IntrusivePtr &rhs) noexcept {
		return Reset(rhs);
	}
	IntrusivePtr &operator=(IntrusivePtr &&rhs) noexcept {
		return Reset(std::move(rhs));
	}
	~IntrusivePtr(){
		X_Dispose();
	}

public:
	constexpr bool IsNonnull() const noexcept {
		return !!x_pElement;
	}
	bool IsUnique() const noexcept {
		const auto pElement = x_pElement;
		if(!pElement){
			return false;
		}
		return static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pElement)->IsUnique();
	}
	std::size_t GetRef() const noexcept {
		const auto pElement = x_pElement;
		if(!pElement){
			return false;
		}
		return static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pElement)->GetRef();
	}
	std::size_t GetWeakRef() const noexcept {
		const auto pElement = x_pElement;
		if(!pElement){
			return 0;
		}
		const auto pView = pElement->x_pView.Load(kAtomicConsume);
		if(!pView){
			return 0;
		}
		return pView->GetRef() - 1;
	}
	constexpr Element *Get() const noexcept {
		return x_pElement;
	}
	Element *Release() noexcept {
		return std::exchange(x_pElement, nullptr);
	}

	IntrusivePtr &Reset(std::nullptr_t = nullptr) noexcept {
		IntrusivePtr().Swap(*this);
		return *this;
	}
	IntrusivePtr &Reset(Element *rhs) noexcept {
		IntrusivePtr(rhs).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusivePtr &Reset(UniquePtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept {
		IntrusivePtr(std::move(rhs)).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusivePtr &Reset(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) noexcept {
		IntrusivePtr(rhs).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusivePtr &Reset(IntrusivePtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept {
		IntrusivePtr(std::move(rhs)).Swap(*this);
		return *this;
	}
	IntrusivePtr &Reset(const IntrusivePtr &rhs) noexcept {
		IntrusivePtr(rhs).Swap(*this);
		return *this;
	}
	IntrusivePtr &Reset(IntrusivePtr &&rhs) noexcept {
		rhs.Swap(*this);
		return *this;
	}

	void Swap(IntrusivePtr &rhs) noexcept {
		using std::swap;
		swap(x_pElement, rhs.x_pElement);
	}

public:
	explicit constexpr operator bool() const noexcept {
		return IsNonnull();
	}
	explicit constexpr operator Element *() const noexcept {
		return Get();
	}

	constexpr Element &operator*() const noexcept {
		ASSERT(IsNonnull());

		return *Get();
	}
	constexpr Element *operator->() const noexcept {
		ASSERT(IsNonnull());

		return Get();
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator==(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Equal()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator==(OtherObjectT *rhs) const noexcept {
		return Equal()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator==(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return Equal()(lhs, rhs.x_pElement);
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator!=(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Unequal()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator!=(OtherObjectT *rhs) const noexcept {
		return Unequal()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator!=(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return Unequal()(lhs, rhs.x_pElement);
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator<(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Less()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator<(OtherObjectT *rhs) const noexcept {
		return Less()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator<(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return Less()(lhs, rhs.x_pElement);
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator>(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Greater()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator>(OtherObjectT *rhs) const noexcept {
		return Greater()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator>(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return Greater()(lhs, rhs.x_pElement);
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator<=(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return LessEqual()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator<=(OtherObjectT *rhs) const noexcept {
		return LessEqual()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator<=(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return LessEqual()(lhs, rhs.x_pElement);
	}

	template<typename OtherObjectT, class OtherDeleterT>
	constexpr bool operator>=(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return GreaterEqual()(x_pElement, rhs.x_pElement);
	}
	template<typename OtherObjectT>
	constexpr bool operator>=(OtherObjectT *rhs) const noexcept {
		return GreaterEqual()(x_pElement, rhs);
	}
	template<typename OtherObjectT>
	friend constexpr bool operator>=(OtherObjectT *lhs, const IntrusivePtr &rhs) noexcept {
		return GreaterEqual()(lhs, rhs.x_pElement);
	}

	friend void swap(IntrusivePtr &lhs, IntrusivePtr &rhs) noexcept {
		lhs.Swap(rhs);
	}
};

template<typename ObjectT, class DeleterT>
const IntrusivePtr<ObjectT, DeleterT> IntrusivePtr<ObjectT, DeleterT>::kNull;

template<typename ObjectT, typename DeleterT = DefaultDeleter<ObjectT>, typename ...ParamsT>
IntrusivePtr<ObjectT, DeleterT> MakeIntrusive(ParamsT &&...vParams){
	static_assert(!std::is_array<ObjectT>::value, "ObjectT shall not be an array type.");
	static_assert(!std::is_reference<ObjectT>::value, "ObjectT shall not be a reference type.");

	return IntrusivePtr<ObjectT, DeleterT>(new ObjectT(std::forward<ParamsT>(vParams)...));
}

template<typename DstT, typename SrcT, class DeleterT>
IntrusivePtr<DstT, DeleterT> StaticPointerCast(IntrusivePtr<SrcT, DeleterT> pSrc) noexcept {
	return IntrusivePtr<DstT, DeleterT>(static_cast<DstT *>(pSrc.Release()));
}
template<typename DstT, typename SrcT, class DeleterT>
IntrusivePtr<DstT, DeleterT> ConstPointerCast(IntrusivePtr<SrcT, DeleterT> pSrc) noexcept {
	return IntrusivePtr<DstT, DeleterT>(const_cast<DstT *>(pSrc.Release()));
}

template<typename DstT, typename SrcT, class DeleterT>
IntrusivePtr<DstT, DeleterT> DynamicPointerCast(IntrusivePtr<SrcT, DeleterT> pSrc) noexcept {
	const auto pTest = dynamic_cast<DstT *>(pSrc.Get());
	if(!pTest){
		return nullptr;
	}
	pSrc.Release();
	return IntrusivePtr<DstT, DeleterT>(pTest);
}

template<typename ObjectT, class DeleterT>
class IntrusiveWeakPtr {
	static_assert(sizeof(IntrusiveBase<ObjectT, DeleterT>) > 0, "IntrusiveBase<ObjectT, DeleterT> is not an object type or is an incomplete type.");
	static_assert(sizeof(dynamic_cast<const volatile IntrusiveBase<ObjectT, DeleterT> *>(DeclVal<ObjectT *>())), "Unable to locate IntrusiveBase for the managed object type.");

	template<typename, class>
	friend class IntrusivePtr;
	template<typename, class>
	friend class WeakIntrusivePtr;

public:
	using Element = ObjectT;
	using Deleter = DeleterT;

	static_assert(noexcept(Deleter()(DeclVal<std::remove_cv_t<Element> *>())), "Deleter must not throw.");

public:
	static const IntrusiveWeakPtr kNull;

private:
	static typename Impl_IntrusivePtr::DeletableBase<DeleterT>::X_WeakView *X_CreateViewFromElement(const volatile Element *pElement){
		if(!pElement){
			return nullptr;
		}
		return Impl_IntrusivePtr::StaticCastOrDynamicCast<const volatile Impl_IntrusivePtr::DeletableBase<DeleterT> *>(pElement)->X_CreateView();
	}
	typename Impl_IntrusivePtr::DeletableBase<DeleterT>::X_WeakView *X_Fork() noexcept {
		const auto pView = x_pView;
		if(pView){
			static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pView)->AddRef();
		}
		return pView;
	}
	typename Impl_IntrusivePtr::DeletableBase<DeleterT>::X_WeakView *X_Release() noexcept {
		return std::exchange(x_pView, nullptr);
	}
	void X_Dispose() noexcept {
		const auto pView = x_pView;
		if(pView){
			if(static_cast<const volatile Impl_IntrusivePtr::RefCountBase *>(pView)->DropRef()){
				delete pView;
			}
		}
#ifndef NDEBUG
		x_pView = (typename Impl_IntrusivePtr::DeletableBase<DeleterT>::X_WeakView *)(std::uintptr_t)0xDEADBEEFDEADBEEF;
#endif
	}

private:
	typename Impl_IntrusivePtr::DeletableBase<DeleterT>::X_WeakView *x_pView;

public:
	constexpr IntrusiveWeakPtr(std::nullptr_t = nullptr) noexcept
		: x_pView(nullptr)
	{
	}
	explicit IntrusiveWeakPtr(Element *rhs)
		: x_pView(X_CreateViewFromElement(rhs))
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename IntrusivePtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusiveWeakPtr(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs)
		: x_pView(X_CreateViewFromElement(rhs.Get()))
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename IntrusiveWeakPtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename IntrusiveWeakPtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusiveWeakPtr(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) noexcept
		: x_pView(rhs.X_Fork())
	{
	}
	template<typename OtherObjectT, typename OtherDeleterT,
		std::enable_if_t<
			std::is_convertible<typename IntrusiveWeakPtr<OtherObjectT, OtherDeleterT>::Element *, Element *>::value &&
				std::is_convertible<typename IntrusiveWeakPtr<OtherObjectT, OtherDeleterT>::Deleter, Deleter>::value,
			int> = 0>
	IntrusiveWeakPtr(IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept
		: x_pView(rhs.X_Release())
	{
	}
	IntrusiveWeakPtr(const IntrusiveWeakPtr &rhs) noexcept
		: x_pView(rhs.X_Fork())
	{
	}
	IntrusiveWeakPtr(IntrusiveWeakPtr &&rhs) noexcept
		: x_pView(rhs.X_Release())
	{
	}
	IntrusiveWeakPtr &operator=(const IntrusiveWeakPtr &rhs) noexcept {
		return Reset(rhs);
	}
	IntrusiveWeakPtr &operator=(IntrusiveWeakPtr &&rhs) noexcept {
		return Reset(std::move(rhs));
	}
	~IntrusiveWeakPtr(){
		X_Dispose();
	}

public:
	bool IsAlive() const noexcept {
		const auto pView = x_pView;
		if(!pView){
			return true;
		}
		return pView->IsOwnerAlive();
	}
	std::size_t GetWeakRef() const noexcept {
		const auto pView = x_pView;
		if(!pView){
			return 0;
		}
		return pView->GetRef() - 1;
	}
	template<typename OtherT = ObjectT>
	IntrusivePtr<OtherT, DeleterT> Lock() const noexcept {
		const auto pView = x_pView;
		if(!pView){
			return nullptr;
		}
		return pView->template LockOwner<OtherT>();
	}

	IntrusiveWeakPtr &Reset(std::nullptr_t = nullptr) noexcept {
		IntrusiveWeakPtr().Swap(*this);
		return *this;
	}
	IntrusiveWeakPtr &Reset(Element *rhs){
		IntrusiveWeakPtr(rhs).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusiveWeakPtr &Reset(const IntrusivePtr<OtherObjectT, OtherDeleterT> &rhs){
		IntrusiveWeakPtr(rhs).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusiveWeakPtr &Reset(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) noexcept {
		IntrusiveWeakPtr(rhs).Swap(*this);
		return *this;
	}
	template<typename OtherObjectT, typename OtherDeleterT>
	IntrusiveWeakPtr &Reset(IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &&rhs) noexcept {
		IntrusiveWeakPtr(std::move(rhs)).Swap(*this);
		return *this;
	}
	IntrusiveWeakPtr &Reset(const IntrusiveWeakPtr &rhs) noexcept {
		IntrusiveWeakPtr(rhs).Swap(*this);
		return *this;
	}
	IntrusiveWeakPtr &Reset(IntrusiveWeakPtr &&rhs) noexcept {
		rhs.Swap(*this);
		return *this;
	}

	void Swap(IntrusiveWeakPtr &rhs) noexcept {
		using std::swap;
		swap(x_pView, rhs.x_pView);
	}

public:
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator==(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Equal()(x_pView, rhs.x_pView);
	}
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator!=(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Unequal()(x_pView, rhs.x_pView);
	}
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator<(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Less()(x_pView, rhs.x_pView);
	}
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator>(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return Greater()(x_pView, rhs.x_pView);
	}
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator<=(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return LessEqual()(x_pView, rhs.x_pView);
	}
	template<typename OtherObjectT, class OtherDeleterT>
	bool operator>=(const IntrusiveWeakPtr<OtherObjectT, OtherDeleterT> &rhs) const noexcept {
		return GreaterEqual()(x_pView, rhs.x_pView);
	}

	friend void swap(IntrusiveWeakPtr &lhs, IntrusiveWeakPtr &rhs) noexcept {
		lhs.Swap(rhs);
	}
};

template<typename ObjectT, class DeleterT>
const IntrusiveWeakPtr<ObjectT, DeleterT> IntrusiveWeakPtr<ObjectT, DeleterT>::kNull;

template<typename ObjectT, class DeleterT>
IntrusiveWeakPtr<ObjectT, DeleterT> Weaken(const IntrusivePtr<ObjectT, DeleterT> &rhs) noexcept {
	return IntrusiveWeakPtr<ObjectT, DeleterT>(rhs);
}

}

#endif

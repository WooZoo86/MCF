// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2016, LH_Mouse. All wrongs reserved.

#ifndef MCF_FUNCTION_FUNCTION_HPP_
#define MCF_FUNCTION_FUNCTION_HPP_

#include "../Core/Exception.hpp"
#include "../SmartPointers/IntrusivePtr.hpp"
#include "../Utilities/Assert.hpp"
#include "../Utilities/DeclVal.hpp"
#include "_MagicalInvoker.hpp"
#include "_ForwardedParam.hpp"
#include <type_traits>
#include <utility>

namespace MCF {

namespace Impl_Function {
	struct FunctorCopier {
		template<typename FuncT>
		IntrusivePtr<FuncT> operator()(const FuncT &vFunc) const {
			return MakeIntrusive<FuncT>(vFunc);
		}
	};
	struct DummyFunctorCopier {
		template<typename FuncT>
		[[noreturn]]
		IntrusivePtr<FuncT> operator()(const FuncT & /* vFunc */) const {
			MCF_THROW(Exception, ERROR_CALL_NOT_IMPLEMENTED, Rcntws::View(L"Function: 该函数对象不允许复制构造。"));
		}
	};

	template<typename RetT, typename ...ParamsT>
	class FunctorBase : public IntrusiveBase<FunctorBase<RetT, ParamsT...>> {
	public:
		virtual ~FunctorBase() = default;

	public:
		virtual RetT Dispatch(Impl_ForwardedParam::ForwardedParam<ParamsT> ...vParams) const = 0;
		virtual IntrusivePtr<FunctorBase> Fork() const = 0;
	};

	template<typename FuncT, typename RetT, typename ...ParamsT>
	class Functor : public FunctorBase<RetT, ParamsT...> {
	private:
		const FuncT x_vFunc;

	public:
		explicit Functor(FuncT vFunc)
			: x_vFunc(std::move(vFunc))
		{
		}

	public:
		RetT Dispatch(Impl_ForwardedParam::ForwardedParam<ParamsT> ...vParams) const override {
			return Impl_MagicalInvoker::MagicalInvoker<RetT>()(x_vFunc, std::forward<ParamsT>(vParams)...);
		}
		IntrusivePtr<FunctorBase<RetT, ParamsT...>> Fork() const override {
			return std::conditional_t<std::is_copy_constructible<FuncT>::value, FunctorCopier, DummyFunctorCopier>()(*this);
		}
	};
}

template<typename PrototypeT>
class Function {
	static_assert((sizeof(PrototypeT), false), "Class template Function instantiated with non-function template type parameter.");
};

template<typename RetT, typename ...ParamsT>
class Function<RetT (ParamsT...)> {
private:
	IntrusivePtr<const Impl_Function::FunctorBase<std::remove_cv_t<RetT>, ParamsT...>> x_pFunctor;

public:
	constexpr Function(std::nullptr_t = nullptr) noexcept
		: x_pFunctor(nullptr)
	{
	}
	template<typename FuncT,
		std::enable_if_t<
			!std::is_base_of<Function, std::decay_t<FuncT>>::value &&
				(std::is_convertible<decltype(Invoke(DeclVal<FuncT>(), DeclVal<ParamsT>()...)), RetT>::value || std::is_void<RetT>::value),
			int> = 0>
	Function(FuncT &&vFunc)
		: x_pFunctor(new Impl_Function::Functor<std::decay_t<FuncT>, std::remove_cv_t<RetT>, ParamsT...>(std::forward<FuncT>(vFunc)))
	{
	}

public:
	bool IsNull() const noexcept {
		return x_pFunctor.IsNull();
	}
	std::size_t GetRefCount() const noexcept {
		return x_pFunctor.GetRefCount();
	}

	Function &Reset(std::nullptr_t = nullptr) noexcept {
		Function().Swap(*this);
		return *this;
	}
	template<typename FuncT>
	Function &Reset(FuncT &&vFunc){
		Function(std::forward<FuncT>(vFunc)).Swap(*this);
		return *this;
	}

	// 后置条件：GetRefCount() <= 1
	void Fork(){
		if(x_pFunctor.GetRefCount() > 1){
			x_pFunctor = x_pFunctor->Fork();
		}
	}

	void Swap(Function &rhs) noexcept {
		using std::swap;
		swap(x_pFunctor, rhs.x_pFunctor);
	}

public:
	explicit operator bool() const noexcept {
		return !IsNull();
	}
	std::remove_cv_t<RetT> operator()(ParamsT ...vParams) const {
		MCF_ASSERT(!IsNull());

		return x_pFunctor->Dispatch(std::forward<ParamsT>(vParams)...); // 值形参当作右值引用传递。
	}

	friend void swap(Function &lhs, Function &rhs) noexcept {
		lhs.Swap(rhs);
	}
};

}

#endif

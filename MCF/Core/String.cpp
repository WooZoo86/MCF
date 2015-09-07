// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2015, LH_Mouse. All wrongs reserved.

#include "../StdMCF.hpp"
#include "String.hpp"
#include "Exception.hpp"
#include "../../MCFCRT/ext/expect.h"

namespace MCF {

namespace {
	// https://en.wikipedia.org/wiki/UTF-8
	// https://en.wikipedia.org/wiki/UTF-16
	// https://en.wikipedia.org/wiki/CESU-8

	template<typename CharT>
	class StringSource {
	private:
		const CharT *x_pchRead;
		const CharT *const x_pchEnd;

	public:
		StringSource(const CharT *pchBegin, const CharT *pchEnd) noexcept
			: x_pchRead(pchBegin), x_pchEnd(pchEnd)
		{
		}

	public:
		__attribute__((__always_inline__))
		explicit operator bool() const noexcept {
			return x_pchRead != x_pchEnd;
		}
		__attribute__((__always_inline__))
		std::uint32_t operator()(){
			if(x_pchRead == x_pchEnd){
				DEBUG_THROW(Exception, ERROR_HANDLE_EOF, "String is truncated"_rcs);
			}
			return static_cast<std::make_unsigned_t<CharT>>(*(x_pchRead++));
		}
	};

	template<class StringObserverT>
	auto MakeStringSource(const StringObserverT &soRead) noexcept {
		return StringSource<typename StringObserverT::Char>(soRead.GetBegin(), soRead.GetEnd());
	}

	template<class PrevT, bool kIsCesu8T>
	class Utf8Decoder {
	private:
		PrevT x_vPrev;

	public:
		explicit Utf8Decoder(PrevT vPrev)
			: x_vPrev(std::move(vPrev))
		{
		}

	public:
		__attribute__((__always_inline__))
		explicit operator bool() const noexcept {
			return !!x_vPrev;
		}
		__attribute__((__always_inline__))
		std::uint32_t operator()(){
			auto u32Point = x_vPrev();
			if(EXPECT_NOT((u32Point & 0x80u) != 0)){
				// 这个值是该码点的总字节数。
				const auto uBytes = CountLeadingZeroes((std::uint8_t)(~u32Point | 1));
				// UTF-8 理论上最长可以编码 6 个字符，但是标准化以后最多只能使用 4 个。
				if(EXPECT_NOT(uBytes - 2 > 2)){ // 2, 3, 4
					DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Invalid UTF-8 leading byte"_rcs);
				}
				u32Point &= (0xFFu >> uBytes);

#define UTF8_DECODER_UNROLLED	\
				{	\
					const auto u32Temp = x_vPrev();	\
					if((u32Temp & 0xC0u) != 0x80u){	\
						DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Invalid UTF-8 non-leading byte"_rcs);	\
					}	\
					u32Point = (u32Point << 6) | (u32Temp & 0x3Fu);	\
				}

				if(uBytes < 3){
					UTF8_DECODER_UNROLLED
				} else if(uBytes == 3){
					UTF8_DECODER_UNROLLED
					UTF8_DECODER_UNROLLED
				} else {
					UTF8_DECODER_UNROLLED
					UTF8_DECODER_UNROLLED
					UTF8_DECODER_UNROLLED
				}

				if(EXPECT_NOT(u32Point > 0x10FFFFu)){
					DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Invalid UTF-32 code point value"_rcs);
				}
				if(EXPECT_NOT(!kIsCesu8T && (u32Point - 0xD800u < 0x800u))){
					DEBUG_THROW(Exception, ERROR_INVALID_DATA, "UTF-32 code point is reserved for UTF-16"_rcs);
				}
			}
			return u32Point;
		}
	};

	template<class PrevT>
	auto MakeUtf8Decoder(PrevT vPrev){
		return Utf8Decoder<PrevT, false>(std::move(vPrev));
	}
	template<class PrevT>
	auto MakeCesu8Decoder(PrevT vPrev){
		return Utf8Decoder<PrevT, true>(std::move(vPrev));
	}

	template<class PrevT>
	class Utf8Encoder {
	private:
		PrevT x_vPrev;
		std::uint32_t x_u32Pending;

	public:
		explicit Utf8Encoder(PrevT vPrev)
			: x_vPrev(std::move(vPrev)), x_u32Pending(0)
		{
		}

	public:
		__attribute__((__always_inline__))
		explicit operator bool() const noexcept {
			return x_u32Pending || !!x_vPrev;
		}
		__attribute__((__always_inline__))
		std::uint32_t operator()(){
			if(EXPECT(x_u32Pending != 0)){
				const auto u32Ret = x_u32Pending & 0xFFu;
				x_u32Pending >>= 8;
				return u32Ret;
			}

			auto u32Point = x_vPrev();
			if(EXPECT_NOT(u32Point > 0x10FFFFu)){
				DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Invalid UTF-32 code point value"_rcs);
			}
			// 这个值是该码点的总字节数。
			const auto uBytes = (34u - CountLeadingZeroes((std::uint32_t)(u32Point | 0x7Fu))) / 5u;
			if(EXPECT_NOT(uBytes > 1)){

#define UTF8_ENCODER_UNROLLED	\
				{	\
					x_u32Pending <<= 8;	\
					x_u32Pending |= (u32Point & 0x3F) | 0x80u;	\
					u32Point >>= 6;	\
				}

				if(uBytes < 3){
					UTF8_ENCODER_UNROLLED
				} else if(uBytes == 3){
					UTF8_ENCODER_UNROLLED
					UTF8_ENCODER_UNROLLED
				} else {
					UTF8_ENCODER_UNROLLED
					UTF8_ENCODER_UNROLLED
					UTF8_ENCODER_UNROLLED
				}

				u32Point |= -0x100u >> uBytes;
			}
			return u32Point;
		}
	};

	template<class PrevT>
	auto MakeUtf8Encoder(PrevT vPrev){
		return Utf8Encoder<PrevT>(std::move(vPrev));
	}

	template<class PrevT>
	class Utf16Decoder {
	private:
		PrevT x_vPrev;

	public:
		explicit Utf16Decoder(PrevT vPrev)
			: x_vPrev(std::move(vPrev))
		{
		}

	public:
		__attribute__((__always_inline__))
		explicit operator bool() const noexcept {
			return !!x_vPrev;
		}
		__attribute__((__always_inline__))
		std::uint32_t operator()(){
			auto u32Point = x_vPrev();
			// 检测前导代理。
			const auto u32Leading = u32Point - 0xD800u;
			if(EXPECT_NOT(u32Leading <= 0x7FFu)){
				if(EXPECT_NOT(u32Leading > 0x3FFu)){
					DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Isolated UTF-16 trailing surrogate"_rcs);
				}
				u32Point = x_vPrev() - 0xDC00u;
				if(EXPECT_NOT(u32Point > 0x3FFu)){
					// 后续代理无效。
					DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Leading surrogate followed by non-trailing-surrogate"_rcs);
				}
				// 将代理对拼成一个码点。
				u32Point = ((u32Leading << 10) | u32Point) + 0x10000u;
			}
			return u32Point;
		}
	};

	template<class PrevT>
	auto MakeUtf16Decoder(PrevT vPrev){
		return Utf16Decoder<PrevT>(std::move(vPrev));
	}

	template<class PrevT>
	class Utf16Encoder {
	private:
		PrevT x_vPrev;
		std::uint32_t x_u32Pending;

	public:
		explicit Utf16Encoder(PrevT vPrev)
			: x_vPrev(std::move(vPrev)), x_u32Pending(0)
		{
		}

	public:
		__attribute__((__always_inline__))
		explicit operator bool() const noexcept {
			return x_u32Pending || !!x_vPrev;
		}
		__attribute__((__always_inline__))
		std::uint32_t operator()(){
			if(EXPECT(x_u32Pending != 0)){
				const auto u32Ret = x_u32Pending;
				x_u32Pending >>= 16;
				return u32Ret;
			}

			auto u32Point = x_vPrev();
			if(EXPECT_NOT(u32Point > 0x10FFFFu)){
				DEBUG_THROW(Exception, ERROR_INVALID_DATA, "Invalid UTF-32 code point value"_rcs);
			}
			if(EXPECT_NOT(u32Point > 0xFFFFu)){
				// 编码成代理对。
				u32Point -= 0x10000u;
				x_u32Pending = (u32Point & 0x3FFu) | 0xDC00u;
				u32Point = (u32Point >> 10) | 0xD800u;
			}
			return u32Point;
		}
	};

	template<class PrevT>
	auto MakeUtf16Encoder(PrevT vPrev){
		return Utf16Encoder<PrevT>(std::move(vPrev));
	}

	template<class StringT, class FilterT>
	void Convert(StringT &strWrite, std::size_t uPos, FilterT vFilter){
		typename StringT::Char achTemp[1024];
		auto pchWrite = std::begin(achTemp);

		if(uPos == strWrite.GetSize()){
			while(vFilter){
				*pchWrite = vFilter();
				if(++pchWrite == std::end(achTemp)){
					strWrite.Append(std::begin(achTemp), pchWrite);
					pchWrite = std::begin(achTemp);
				}
			}
			if(pchWrite != std::begin(achTemp)){
				strWrite.Append(std::begin(achTemp), pchWrite);
			}
		} else {
			while(vFilter){
				*pchWrite = vFilter();
				if(++pchWrite == std::end(achTemp)){
					strWrite.Replace((std::ptrdiff_t)uPos, (std::ptrdiff_t)uPos, std::begin(achTemp), pchWrite);
					uPos += CountOf(achTemp);
					pchWrite = std::begin(achTemp);
				}
			}
			if(pchWrite != std::begin(achTemp)){
				strWrite.Replace((std::ptrdiff_t)uPos, (std::ptrdiff_t)uPos, std::begin(achTemp), pchWrite);
			}
		}
	}
}

template class String<StringType::NARROW>;
template class String<StringType::WIDE>;
template class String<StringType::UTF8>;
template class String<StringType::UTF16>;
template class String<StringType::UTF32>;
template class String<StringType::CESU8>;
template class String<StringType::ANSI>;

// UTF-8
template<>
UnifiedStringObserver NarrowString::Unify(UnifiedString &usTempStorage, const NarrowStringObserver &nsoSrc){
	usTempStorage.Reserve(nsoSrc.GetSize());
	Convert(usTempStorage, 0, MakeUtf8Decoder(MakeStringSource(nsoSrc)));
	return usTempStorage;
}
template<>
void NarrowString::Deunify(NarrowString &nsDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	nsDst.ReserveMore(usoSrc.GetSize() * 2);
	Convert(nsDst, uPos, MakeUtf8Encoder(MakeStringSource(usoSrc)));
}

// UTF-16
template<>
UnifiedStringObserver WideString::Unify(UnifiedString &usTempStorage, const WideStringObserver &wsoSrc){
	usTempStorage.Reserve(wsoSrc.GetSize());
	Convert(usTempStorage, 0, MakeUtf16Decoder(MakeStringSource(wsoSrc)));
	return usTempStorage;
}
template<>
void WideString::Deunify(WideString &wsDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	wsDst.ReserveMore(usoSrc.GetSize());
	Convert(wsDst, uPos, MakeUtf16Encoder(MakeStringSource(usoSrc)));
}

// UTF-8
template<>
UnifiedStringObserver Utf8String::Unify(UnifiedString &usTempStorage, const Utf8StringObserver &u8soSrc){
	usTempStorage.Reserve(u8soSrc.GetSize());
	Convert(usTempStorage, 0, MakeUtf8Decoder(MakeStringSource(u8soSrc)));
	return usTempStorage;
}
template<>
void Utf8String::Deunify(Utf8String &u8sDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	u8sDst.ReserveMore(usoSrc.GetSize() * 3);
	Convert(u8sDst, uPos, MakeUtf8Encoder(MakeStringSource(usoSrc)));
}

// UTF-16
template<>
UnifiedStringObserver Utf16String::Unify(UnifiedString &usTempStorage, const Utf16StringObserver &u16soSrc){
	usTempStorage.Reserve(u16soSrc.GetSize());
	Convert(usTempStorage, 0, MakeUtf16Decoder(MakeStringSource(u16soSrc)));
	return usTempStorage;
}
template<>
void Utf16String::Deunify(Utf16String &u16sDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	u16sDst.ReserveMore(usoSrc.GetSize());
	Convert(u16sDst, uPos, MakeUtf16Encoder(MakeStringSource(usoSrc)));
}

// UTF-32
template<>
UnifiedStringObserver Utf32String::Unify(UnifiedString & /* usTempStorage */, const Utf32StringObserver &u32soSrc){
	return u32soSrc;
}
template<>
void Utf32String::Deunify(Utf32String &u32sDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	u32sDst.Replace((std::ptrdiff_t)uPos, (std::ptrdiff_t)uPos, usoSrc);
}

// CESU-8
template<>
UnifiedStringObserver Cesu8String::Unify(UnifiedString &usTempStorage, const Cesu8StringObserver &cu8soSrc){
	usTempStorage.Reserve(cu8soSrc.GetSize());
	Convert(usTempStorage, 0, MakeUtf16Decoder(MakeCesu8Decoder(MakeStringSource(cu8soSrc))));
	return usTempStorage;
}
template<>
void Cesu8String::Deunify(Cesu8String &cu8sDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	cu8sDst.ReserveMore(usoSrc.GetSize() * 3);
	Convert(cu8sDst, uPos, MakeUtf8Encoder(MakeUtf16Encoder(MakeStringSource(usoSrc))));
}

// ANSI
template<>
UnifiedStringObserver AnsiString::Unify(UnifiedString &usTempStorage, const AnsiStringObserver &asoSrc){
	if(!asoSrc.IsEmpty()){
		WideString wsTemp;
		wsTemp.Resize(asoSrc.GetSize());
		const unsigned uCount = (unsigned)::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
			asoSrc.GetBegin(), (int)asoSrc.GetSize(), wsTemp.GetData(), (int)wsTemp.GetSize());
		if(uCount == 0){
			DEBUG_THROW(SystemError, "MultiByteToWideChar"_rcs);
		}
		usTempStorage.Reserve(uCount);
		Convert(usTempStorage, 0, MakeUtf16Decoder(MakeStringSource(WideStringObserver(wsTemp.GetData(), uCount))));
	}
	return usTempStorage;
}
template<>
void AnsiString::Deunify(AnsiString &ansDst, std::size_t uPos, const UnifiedStringObserver &usoSrc){
	if(!usoSrc.IsEmpty()){
		WideString wsTemp;
		wsTemp.Reserve(usoSrc.GetSize());
		Convert(wsTemp, 0, MakeUtf16Encoder(MakeStringSource(usoSrc)));

		AnsiString ansConverted;
		ansConverted.Resize(wsTemp.GetSize() * 2);
		const unsigned uCount = (unsigned)::WideCharToMultiByte(CP_ACP, 0,
			wsTemp.GetData(), (int)wsTemp.GetSize(), ansConverted.GetData(), (int)ansConverted.GetSize(), nullptr, nullptr);
		if(uCount == 0){
			DEBUG_THROW(SystemError, "WideCharToMultiByte"_rcs);
		}
		ansDst.Replace((std::ptrdiff_t)uPos, (std::ptrdiff_t)uPos, ansConverted.GetData(), uCount);
	}
}

}

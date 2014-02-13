// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2014. LH_Mouse. All wrongs reserved.

#ifndef __MCF_LZMA_HPP__
#define __MCF_LZMA_HPP__

#include <memory>
#include <functional>
#include <utility>
#include <cstddef>

namespace MCF {

class LzmaEncoder {
private:
	class xDelegate;
private:
	const std::unique_ptr<xDelegate> xm_pDelegate;
public:
	LzmaEncoder(std::function<std::pair<void *, std::size_t>(std::size_t)> fnDataCallback, int nLevel = 5, std::uint32_t u32DictSize = 1u << 24);
	~LzmaEncoder();
public:
	void Abort() noexcept;
	void Update(const void *pData, std::size_t uSize);
	void Finalize();
};

class LzmaDecoder {
private:
	class xDelegate;
private:
	const std::unique_ptr<xDelegate> xm_pDelegate;
public:
	LzmaDecoder(std::function<std::pair<void *, std::size_t>(std::size_t)> fnDataCallback);
	~LzmaDecoder();
public:
	void Abort() noexcept;
	void Update(const void *pData, std::size_t uSize);
	void Finalize();
};

}

#endif

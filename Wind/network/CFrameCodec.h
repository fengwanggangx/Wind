#ifndef __CFRAME_CODEC_H__
#define __CFRAME_CODEC_H__
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace net
{
	/*
┌─────────────┬────────────────┐
 4字节长度字段 │ payload        
 辅助协议信息  │ 真正的业务消息
└─────────────┴────────────────┘
	*/

	class CFrameCodec final
	{
		public:
			explicit CFrameCodec(std::size_t nMaxFrameSize = 8 * 1024 * 1024);
			bool Append(const void* pData, std::size_t nLength, std::vector<std::string>& frames);
			void Reset();
			bool HasError() const;
			static std::string Encode(const std::string& payload);

		private:
			std::size_t m_nMaxFrameSize;
			std::vector<std::uint8_t> m_buffer;
			bool m_bError{false};
	};
} // namespace net
#endif

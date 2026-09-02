#include "CFrameCodec.h"
namespace net
{
	CFrameCodec::CFrameCodec(std::size_t nMaxFrameSize) : m_nMaxFrameSize(nMaxFrameSize)
	{
		m_buffer.reserve(4096);
	}
	bool CFrameCodec::Append(const void* pData, std::size_t nLength, std::vector<std::string>& frames)
	{
		if (m_bError || ((nullptr == pData) && (nLength != 0)))
		{
			return false;
		}
		const auto* pBytes = static_cast<const std::uint8_t*>(pData);
		m_buffer.insert(m_buffer.end(), pBytes, pBytes + nLength);
		std::size_t nOffset = 0;
		while ((m_buffer.size() - nOffset) >= sizeof(std::uint32_t))
		{
			std::uint32_t nFrameLength = (static_cast<std::uint32_t>(m_buffer[nOffset]) << 24) |
										 (static_cast<std::uint32_t>(m_buffer[nOffset + 1]) << 16) |
										 (static_cast<std::uint32_t>(m_buffer[nOffset + 2]) << 8) |
										 static_cast<std::uint32_t>(m_buffer[nOffset + 3]);
			if ((nFrameLength == 0) || (nFrameLength > m_nMaxFrameSize))
			{
				m_bError = true;
				return false;
			}
			if ((m_buffer.size() - nOffset - sizeof(std::uint32_t)) < nFrameLength)
			{
				break;
			}
			const char* pPayload = reinterpret_cast<const char*>(m_buffer.data() + nOffset + sizeof(std::uint32_t));
			frames.emplace_back(pPayload, nFrameLength);
			nOffset += sizeof(std::uint32_t) + nFrameLength;
		}
		if (nOffset != 0)
		{
			m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(nOffset));
		}
		return true;
	}
	void CFrameCodec::Reset()
	{
		m_buffer.clear();
		m_bError = false;
	}
	bool CFrameCodec::HasError() const
	{
		return m_bError;
	}
	std::string CFrameCodec::Encode(const std::string& payload)
	{
		std::uint32_t nLength = static_cast<std::uint32_t>(payload.size());
		std::string frame(sizeof(std::uint32_t), '\0');
		frame[0] = static_cast<char>((nLength >> 24) & 0xFF);
		frame[1] = static_cast<char>((nLength >> 16) & 0xFF);
		frame[2] = static_cast<char>((nLength >> 8) & 0xFF);
		frame[3] = static_cast<char>(nLength & 0xFF);
		frame.append(payload);
		return frame;
	}
} // namespace net

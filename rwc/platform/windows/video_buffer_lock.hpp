#pragma once

#include <mfapi.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <utility>
#include <expected>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace rwc
{
    struct video_buffer_result
    {
        uint8_t* scanline0;
        uint32_t stride;
        uint32_t size;
    };

    class video_buffer_lock
    {
    public:
        video_buffer_lock(ComPtr<IMFMediaBuffer> buffer)
            : m_buffer{ std::move(buffer) }
            , m_buffer_2d{ nullptr }
            , m_locked{ false }
        {
            static_cast<void>(m_buffer.As(&m_buffer_2d));
        }

        std::expected<video_buffer_result, HRESULT> lock(int32_t default_stride, uint32_t height)
        {
            // Use non-2D version.
            uint8_t* buffer_data = {};
            DWORD size = 0;
            LONG stride = {};
            HRESULT hr = S_OK;
            video_buffer_result result = {};

            if (m_buffer_2d)
            {
                hr = m_buffer_2d->Lock2D(&buffer_data, &stride);
                if (SUCCEEDED(hr))
                    hr = m_buffer_2d->GetContiguousLength(&size);
            }
            else 
            {
                stride = default_stride;
                hr = m_buffer->Lock(&buffer_data, nullptr, &size);
            }

            if (SUCCEEDED(hr))
            {
                if (stride < 0)
                    result.scanline0 = buffer_data + std::abs(stride) * LONG(height - 1);
                else 
                    result.scanline0 = buffer_data;

                result.stride = static_cast<uint32_t>(std::abs(stride));
                result.size = size;
                m_locked = true;

                return result;
            }
            
            return std::unexpected{ hr };
        }

        void unlock()
        {
            if (m_locked)
            {
                if (m_buffer_2d)
                    static_cast<void>(m_buffer_2d->Unlock2D());
                else
                    static_cast<void>(m_buffer->Unlock());

                m_locked = false;
            }
        }

        ~video_buffer_lock()
        {
            unlock();
        }

    private:
        ComPtr<IMFMediaBuffer> m_buffer;
        ComPtr<IMF2DBuffer> m_buffer_2d;
        bool m_locked;
    };
}

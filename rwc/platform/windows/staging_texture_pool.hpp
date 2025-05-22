#pragma once

#include <vector>

#include <wrl/client.h>
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

namespace rwc
{
    class staging_texture_pool
    {
    public:
        staging_texture_pool() = default;

        bool create(size_t num_buffers, ID3D11Device* device, const D3D11_TEXTURE2D_DESC& tex_desc)
        {
            m_pool.resize(num_buffers);
            bool failed = false;

            for (size_t i{}; i < num_buffers; ++i)
            {
                auto hr = device->CreateTexture2D(&tex_desc, nullptr, &m_pool[i]);
                failed = FAILED(hr);
                if (failed)
                    break;
            }

            if (failed)
            {
                m_pool.clear();
                return false;
            }

            return true;
        }

        ID3D11Texture2D* get_read_texture()
        {
            size_t read_index = (m_current_index + 1) % m_pool.size();
            return m_pool[read_index].Get();
        }

        ID3D11Texture2D* get_write_texture()
        {
            size_t write_index = m_current_index % m_pool.size();
            return m_pool[write_index].Get();
        }

        void update_indices()
        {
            ++m_current_index;
        }

        size_t current_index() const noexcept
        {
            return m_current_index;
        }

        size_t buffer_count() const noexcept
        {
            return m_pool.size();
        }

        ~staging_texture_pool() = default;
    private:
        size_t m_current_index{};
        std::vector<ComPtr<ID3D11Texture2D>> m_pool;
    };
}

#include <Utils/StringWriter.h>
#include <cassert>

namespace jucyaudio
{
    StringWriter::StringWriter(StringWriter &&source) noexcept
        : m_write_position{source.m_write_position},
          m_dynamic_size{source.m_dynamic_size},
          m_dynamic_buffer{source.m_dynamic_buffer},
          m_builtin_buffer{}
    {
        source.m_write_position = 0;
        source.m_dynamic_size = 0;
        source.m_dynamic_buffer = nullptr;

        // if (and only if) the origin still uses the cache, we need to do so as well
        if (!m_dynamic_buffer)
        {
            assert(m_write_position < std::size(m_builtin_buffer));
            memcpy(m_builtin_buffer, source.m_builtin_buffer, m_write_position);
        }
    }

    StringWriter &StringWriter::operator=(StringWriter &&source) noexcept
    {
        assert(this != &source);
        if (m_dynamic_buffer)
        {
            free(m_dynamic_buffer);
        }

        m_write_position = source.m_write_position;
        source.m_write_position = 0;

        m_dynamic_size = source.m_dynamic_size;
        source.m_dynamic_size = 0;

        m_dynamic_buffer = source.m_dynamic_buffer;
        source.m_dynamic_buffer = nullptr;

        // if (and only if) the origin still uses the cache, we need to do so as well
        if (!m_dynamic_buffer)
        {
            assert(m_write_position < std::size(m_builtin_buffer));
            memcpy(m_builtin_buffer, source.m_builtin_buffer, m_write_position);
        }
        return *this;
    }

    bool StringWriter::append(char c)
    {
        if (m_dynamic_buffer == nullptr)
        {
            if (m_write_position < std::size(m_builtin_buffer))
            {
                m_builtin_buffer[m_write_position++] = c;
                return true;
            }
        }
        else if (m_write_position < m_dynamic_size)
        {
            m_dynamic_buffer[m_write_position++] = c;
            return true;
        }

        if (m_dynamic_buffer)
        {
            char *np = (char *)realloc(m_dynamic_buffer, m_dynamic_size * 2);
            if (!np)
                return false;

            m_dynamic_buffer = np;
            m_dynamic_size *= 2;
            m_dynamic_buffer[m_write_position++] = c;
            return true;
        }
        const auto np{static_cast<char *>(malloc(std::size(m_builtin_buffer) * 2))};
        if (!np)
            return false;

        memcpy(np, m_builtin_buffer, m_write_position);
        m_dynamic_buffer = np;
        m_dynamic_size = std::size(m_builtin_buffer) * 2;
        m_dynamic_buffer[m_write_position++] = c;
        return true;
    }

    void StringWriter::correct(int bytes_written) const
    {
        if (m_dynamic_buffer == nullptr)
        {
            if ((m_write_position + bytes_written) >= std::size(m_builtin_buffer))
            {
                assert(false);
            }
        }
        else
        {
            if ((m_write_position + bytes_written) >= m_dynamic_size)
            {
                assert(false);
            }
        }

        if (bytes_written >= 0)
        {
            m_write_position += bytes_written;
        }
        else
        {
            assert(false);
        }
    }

    char *StringWriter::ensure_free_space(size_t space_needed)
    {
        const size_t space_total = m_write_position + space_needed;

        // if we're still using the stack buffer
        if (m_dynamic_buffer == nullptr)
        {
            // and have enough space left
            if (space_total < std::size(m_builtin_buffer))
            {
                return m_builtin_buffer + m_write_position;
            }
        }

        // if we are using the dynamic buffer and have enough space left
        if (space_total < m_dynamic_size)
        {
            return m_dynamic_buffer + m_write_position;
        }

        // we DO have a dynamic buffer, but not enough size: allocate large enough
        if (m_dynamic_buffer)
        {
            size_t size_needed = m_dynamic_size * 2;
            while (size_needed < space_total)
            {
                size_needed *= 2;
            }

            char *np = (char *)realloc(m_dynamic_buffer, size_needed);
            if (!np)
                return nullptr;

            m_dynamic_buffer = np;
            m_dynamic_size = size_needed;
            return m_dynamic_buffer + m_write_position;
            ;
        }
        size_t size_needed = std::size(m_builtin_buffer) * 2;
        while (size_needed < space_total)
        {
            size_needed *= 2;
        }
        const auto np{static_cast<char *>(malloc(size_needed))};
        if (!np)
            return nullptr;

        memcpy(np, m_builtin_buffer, m_write_position);
        m_dynamic_buffer = np;
        m_dynamic_size = size_needed;
        return m_dynamic_buffer + m_write_position;
        ;
    }

    bool StringWriter::assign(const StringWriter &objectSrc)
    {
        if (this == &objectSrc)
            return true;

        clear();

        // we need to copy EITHER the cache OR the dynamic buffer
        if (objectSrc.m_dynamic_buffer)
        {
            m_dynamic_buffer = static_cast<char *>(malloc(objectSrc.m_dynamic_size));

            if (!m_dynamic_buffer)
                return false;

            // but we need to copy only the used bytes
            memcpy(m_dynamic_buffer, objectSrc.m_dynamic_buffer, objectSrc.m_write_position);

            m_write_position = objectSrc.m_write_position;
            m_dynamic_size = objectSrc.m_dynamic_size;
            return true;
        }
        else
        {
            clear();
            m_write_position = objectSrc.m_write_position;
            if (m_write_position)
            {
                memcpy(m_builtin_buffer, objectSrc.m_builtin_buffer, m_write_position);
            }
            return true;
        }
    }
} // namespace jucyaudio

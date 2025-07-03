#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <format>

namespace jucyaudio
{
    class StringWriter final
    {
    public:
        StringWriter()
            : m_write_position{0},
              m_dynamic_size{0},
              m_dynamic_buffer{nullptr},
              m_builtin_buffer{}
        {
        }

        StringWriter(const StringWriter &source)
            : m_write_position{0},
              m_dynamic_size{0},
              m_dynamic_buffer{nullptr},
              m_builtin_buffer{}
        {
            assign(source);
        }

        StringWriter &operator=(const StringWriter &source)
        {
            assign(source);
            return *this;
        }

        StringWriter(StringWriter &&source) noexcept;

        StringWriter &operator=(StringWriter &&source) noexcept;

        ~StringWriter()
        {
            clear();
        }

        bool empty() const
        {
            return (m_write_position == 0);
        }

        std::string as_string() const
        {
            // this is needed to ensure that the string is zero-terminated
            const_cast<StringWriter *>(this)->append('\0');
            --m_write_position;
            return m_dynamic_buffer ? m_dynamic_buffer : m_builtin_buffer;
        }

        bool newline()
        {
            return append("\n");
        }

        bool append(char c);

        bool append(std::string_view s)
        {
            if (s.empty() || !s.data())
                return true;

            const auto len = s.size();
            char *wp = ensure_free_space(len);
            if (!wp)
                return false;

            memcpy(wp, s.data(), len);
            m_write_position += len;
            return true;
        }

        template <typename... Args> bool append_formatted(const std::string_view text, Args &&...args)
        {
            return append(std::vformat(text, std::make_format_args(args...)));
        }

        void clear()
        {
            if (m_dynamic_buffer)
            {
                free(m_dynamic_buffer);
                m_dynamic_buffer = nullptr;
                m_dynamic_size = 0;
            }
            m_write_position = 0;
        }

    private:
        void correct(int bytes_written) const;

        char *ensure_free_space(size_t space_needed);

        bool assign(const StringWriter &objectSrc);

    private:
        mutable size_t m_write_position;
        size_t m_dynamic_size;
        char *m_dynamic_buffer;
        char m_builtin_buffer[1024];
    };

} // namespace jucyaudio

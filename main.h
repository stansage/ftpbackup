#ifndef MAIN_H
#define MAIN_H

#include <Poco/Util/Application.h>

#ifdef ASSERT_LOG
    #undef ASSERT_LOG
#else
    #define ASSERT_LOG(expr) \
    if (false == (expr)) { \
        poco_assert(expr); \
        Poco::Util::Application::instance().logger().log(Poco::AssertionViolationException("ASSERT_LOG")); \
        return; \
    }

    #define ASSERT_LOG_RETURN(x, ret) \
    if (false == (expr)) { \
        poco_assert(expr); \
        Poco::Util::Application::instance().logger().log(Poco::AssertionViolationException("ASSERT_LOG_RETURN")); \
        return ret; \
    }

    #define ASSERT_THROW(expr) \
    if (false == (expr)) { \
        poco_assert(expr); \
        throw Poco::AssertionViolationException("ASSERT_THROW"); \
    }
#endif

struct App
{
    static const std::string EmptyString;

    static Poco::Util::Application &get() { return Poco::Util::Application::instance(); }
    static Poco::Logger &logger() { return get().logger(); }
    static std::string config(const std::string& name, const std::string& def = EmptyString)
    {
        return get().config().getString(name, def);
    }

    static std::string lastToken(const std::string& str, char sep)
    {
        if (!sep) return str;
        const size_t pos = str.find_last_of(sep);
        return std::string::npos == pos ? str : str.substr(pos + 1, str.size() - pos);
    }

};

#endif // MAIN_H

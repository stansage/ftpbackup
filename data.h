#ifndef DATA_H
#define DATA_H

#include <vector>
#include <Poco/DateTime.h>
#include <Poco/SharedPtr.h>

class Data
{

public:
    class Singleton;

    typedef Poco::Timestamp::TimeVal TimePoint_t;

    Data();
    ~Data();

    struct File
    {
        typedef Poco::SharedPtr<File> Ptr_t;
        typedef std::vector<Ptr_t> List_t;

        enum Status { Added = 0, Modified = 1, Deleted = -1 };

        unsigned id, crc32;
        std::string fullName, modifyDate;
        bool isDirectory;

        virtual void setStatus(File::Status status) = 0;

        bool isDeleted() const { return unsigned(Deleted) == crc32; }
    };

    struct Ignore
    {
        typedef Poco::SharedPtr<Ignore> Ptr_t;
        typedef std::vector<Ptr_t> List_t;

        enum Attribute { AttributeExt, AttributePath, CountOfAttributes };

        Attribute attribute;
        std::string operand;

        bool isValid() const
        {
            return -1 != attribute && !operand.empty();
        }
    };

    struct Site
    {
        typedef Poco::SharedPtr<Site> Ptr_t;
        typedef std::vector<Ptr_t> List_t;

        unsigned id;
        std::string login, password;

        virtual File::List_t files(TimePoint_t tp = 0) const = 0;
        virtual Ignore::List_t ignores()  const = 0;
        virtual File::Ptr_t createFile(const std::string& fullName,
                                       const std::string& modifyDate,
                                       bool isDirectory) const = 0;
    };

    const Site::List_t& sites() const { return _sites; }
    Site::Ptr_t siteById(unsigned id) const;

    static TimePoint_t currentTimePoint();

private:
    Site::List_t _sites;

    static Singleton*_singleton;
};

#endif // DATA_H

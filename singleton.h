#ifndef SINGLETON_H
#define SINGLETON_H

#include "data.h"

#include <Poco/Data/Session.h>
#include <Poco/Data/Binding.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/Data/MySQL/Connector.h>
#include <Poco/Data/MySQL/Binder.h>

class Data::Singleton
{
    // lifehack for Poco library version 1.3.6p1 (MYSQL::Binder not support unsigned int)
    class UB : public Poco::Data::Binding<unsigned>
    {
        typedef Poco::Data::Binding<unsigned> Base_t;

    public:

        explicit UB(const unsigned& val) : Base_t(val) { }

        void bind(std::size_t pos)
        {
            Base_t::bind(pos);
            MYSQL_BIND* pb = static_cast<Poco::Data::MySQL::Binder*>(
                getBinder())->getBindArray();
            pb[pos].is_unsigned = true;
        }
    };

    struct BindCache
    {
        unsigned fileId, fileCrc32;
        std::string fileFullName, fileModifyDate;
        bool fileIsDirectory;
        short fileStatus;
        unsigned siteId;
        TimePoint_t timePoint;
    };

public:
    typedef Poco::SharedPtr<Poco::Data::RecordSet> RecordSetPtr_t;

    Singleton();

    Poco::Data::Session& dbSession() { return _ses; }

    RecordSetPtr_t selectFiles(unsigned siteId, TimePoint_t tp = 0);
    RecordSetPtr_t selectIgnores(unsigned siteId);

    void addFile(unsigned siteId, const File& file);

    void updFile(unsigned siteId, const File& file);
    void delFile(unsigned siteId, const File& file);

    void incrementUsage();
    bool decrementUsage();

    static Singleton &getInstance();

private:
    void bindCache(unsigned siteId, const File& file);

private:
    unsigned _counter;
    Poco::FastMutex _mutex;

    BindCache _cache;
    Poco::Data::Session _ses;
    Poco::Data::Statement _selectTrunk, _selectHistory, _selectIgnores,
        _insFile, _updFile, _insHistory;
};

#endif // SINGLETON_H

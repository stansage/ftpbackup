#include "data.h"
#include "singleton.h"
#include "main.h"

using Poco::Data::Statement;
using Poco::Data::RecordSet;
using Poco::Data::MySQL::Connector;

class FileImpl : public Data::File
{
public:
    // Select file columns position
    enum { FileId, FileCrc32, FileFullName, FileIsDirectory, FileModifyDate };

    FileImpl(unsigned siteId, Data::Singleton::RecordSetPtr_t rs);

    void setStatus(File::Status status);

private:
    unsigned _siteId;
};

FileImpl::FileImpl(unsigned siteId, Data::Singleton::RecordSetPtr_t rs) : _siteId(siteId)
{
    if (!rs) return;
    id = rs->value(FileId).convert<unsigned>();
    crc32 = rs->value(FileCrc32).convert<unsigned>();
    isDirectory = rs->value(FileIsDirectory).convert<bool>();
    fullName = rs->value(FileFullName).convert<std::string>();
    modifyDate = rs->value(FileModifyDate).convert<std::string>();
}

void FileImpl::setStatus(File::Status status)
{
    typedef void (Data::Singleton::*ImplMember_t)(unsigned, const File&);
    ImplMember_t im;
    switch (status) {
        case File::Added:       im = &Data::Singleton::addFile; break;
        case File::Modified:    im = &Data::Singleton::updFile; break;
        case File::Deleted:     im = &Data::Singleton::delFile; break;
        default: throw Poco::LogicException("File::setStatus failed, unknown value");
    }
    (Data::Singleton::getInstance().*im)(_siteId, *this);
}

//-----------------------------------------------------------------------------
class IgnoreImpl : public Data::Ignore
{
public:
    // Select ignore columns position
    enum { IgnoreAttribute, IgnoreOperand, IgnoreIsNot };

    IgnoreImpl(Data::Singleton::RecordSetPtr_t rs);
};

IgnoreImpl::IgnoreImpl(Data::Singleton::RecordSetPtr_t rs)
{
    if (!rs) return;

    attribute = Attribute(-1);

    std::string val = rs->value(IgnoreAttribute).convert<std::string>();
    if (!Poco::icompare(std::string("ext"), val))
        attribute = AttributeExt;
    else if (!Poco::icompare(std::string("path"), val))
        attribute = AttributePath;

    operand = rs->value(IgnoreOperand).convert<std::string>();
}

//-----------------------------------------------------------------------------
class SiteImpl : public Data::Site
{
    Data::File::List_t files(Data::TimePoint_t tp) const;

    Data::Ignore::List_t ignores() const;

    Data::File::Ptr_t createFile(const std::string& fullName,
                                 const std::string& modifyDate,
                                 bool isDirectory) const;
};

Data::File::List_t SiteImpl::files(Data::TimePoint_t tp) const
{
    Data::Singleton::RecordSetPtr_t rs = Data::Singleton::getInstance().selectFiles(id, tp);
    if (!rs) return Data::File::List_t();

    // One record to one file
    int i = 0;
    Data::File::List_t  ret(rs->rowCount());
    for (bool more = rs->moveFirst(); more; more = rs->moveNext(), ++i)
        ret[i].assign(new FileImpl(id, rs));
    return ret;
}

Data::Ignore::List_t SiteImpl::ignores() const
{
    Data::Singleton::RecordSetPtr_t rs = Data::Singleton::getInstance().selectIgnores(id);
    if (!rs) return Data::Ignore::List_t();

    // One record to one ignore
    int i = 0;
    Data::Ignore::List_t  ret(rs->rowCount());
    for (bool more = rs->moveFirst(); more; more = rs->moveNext(), ++i)
        ret[i].assign(new IgnoreImpl(rs));
    return ret;
}

Data::File::Ptr_t SiteImpl::createFile(const std::string& fullName,
    const std::string& modifyDate, bool isDirectory) const
{
    Data::File::Ptr_t ret(
        new FileImpl(id, Data::Singleton::RecordSetPtr_t()));

    ret->id = 0;
    ret->crc32 = 0;
    ret->fullName = fullName;
    ret->modifyDate = modifyDate;
    ret->isDirectory = isDirectory;

    return ret;
}

//=============================================================================
/*
    Data class contains singleton Impl instance
      so global usage per static methods only
*/

Data::Singleton* Data::_singleton = 0;

Data::Data()
{
    if (_singleton)
        _singleton->incrementUsage();
    else {
        Connector::registerConnector();
        _singleton = new Singleton();
    }

    Statement select(_singleton->dbSession());
    select << "SELECT id, clientLogin, clientPasswd FROM ftp_mapping";

    // Cache sites list
    _sites.resize(select.execute());
    if (!_sites.empty()) {
        int i = 0;
        RecordSet rs(select);
        // One record to one site
        for (bool more = rs.moveFirst(); more; more = rs.moveNext(), ++i)
        {
            Site::Ptr_t site(new SiteImpl());
            site->id = rs[0].extract<unsigned>();
            site->login = rs[1].extract<std::string>();
            site->password = rs[2].extract<std::string>();
            _sites[i] = site;
        }
    }
}

Data::~Data()
{
    if (_singleton && _singleton->decrementUsage()) {
        delete _singleton;
        _singleton = 0;
        Connector::unregisterConnector();
    }
}


Data::Site::Ptr_t Data::siteById(unsigned id) const
{
    for (size_t i = 0, count = _sites.size(); i < count; ++i)
        if (sites()[i]->id == id) return _sites[i];
    return Site::Ptr_t();
}

Data::TimePoint_t Data::currentTimePoint()
{
    static TimePoint_t tp = Poco::Timestamp().epochMicroseconds();
    return tp;
}

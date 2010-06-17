#include "singleton.h"
#include "main.h"

#include <Poco/Data/SessionFactory.h>
#include <Poco/Data/MySQL/SessionImpl.h>

using Poco::Data::use;
using Poco::Data::SessionFactory;
using Poco::Data::Statement;
using Poco::Data::RecordSet;
using Poco::Data::MySQL::Connector;
using Poco::Data::MySQL::SessionImpl;


Data::Singleton::Singleton() : _counter(0),
    _ses(SessionFactory::instance().create(Connector::KEY, App::config("mysql.connection"))),
    _selectTrunk(_ses), _selectHistory(_ses), _selectIgnores(_ses),
    _insFile(_ses), _updFile(_ses), _insHistory(_ses)
{
    // Select files with last changed attributes
    _selectTrunk << "SELECT f.id, f.crc32, f.fullName, f.isDirectory, f.modifyDate"
        " FROM ftp_backup_files f join ftp_backup_history h"
        " on h.fileId = f.id  and h.timePoint = f.timePoint"
        " and h.fileStatus <> -1 WHERE f.siteId = ?", new UB(_cache.siteId);

    // Select files by timestamp revision. Column mapping crc32 => fileStatus, modifyDate => timePoint
    _selectHistory << "SELECT f.id, CAST(h.fileStatus AS UNSIGNED),"
        " f.fullName, f.isDirectory, CAST(MAX(h.timePoint) AS CHAR)"
        " FROM ftp_backup_files f join ftp_backup_history h on h.fileId = f.id"
        " WHERE h.timePoint <= ? and f.siteId = ?"
        " GROUP BY f.id, h.fileStatus, f.fullName, f.isDirectory",
        use(_cache.timePoint), new UB(_cache.siteId);

    _selectIgnores << "SELECT DISTINCT attribute, operand"
        " FROM ftp_backup_ignores WHERE siteId = ?", new UB(_cache.siteId);

    //Insert only new found files
    _insFile << "INSERT INTO ftp_backup_files"
        " (siteId, crc32, timePoint, fullName, modifyDate, isDirectory)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        new UB(_cache.siteId), new UB(_cache.fileCrc32), use(_cache.timePoint),
        use(_cache.fileFullName), use(_cache.fileModifyDate), use(_cache.fileIsDirectory);

    // Change attributes on any modification
    // Or update timePoint to current backup operation timestamp
    _updFile << "UPDATE ftp_backup_files set crc32 = ?, timePoint = ?,"
        " modifyDate = ?, isDirectory = ? WHERE id = ?",
        new UB(_cache.fileCrc32), use(_cache.timePoint), use(_cache.fileModifyDate),
        use(_cache.fileIsDirectory), new UB(_cache.fileId);

    // Save all file statatus changes (INS, UPD and DEL)
    _insHistory << "INSERT INTO ftp_backup_history"
        " (fileId, timePoint, fileStatus) VALUES (?, ?, ?)",
        new UB(_cache.fileId), use(_cache.timePoint), use(_cache.fileStatus);
}

Data::Singleton::RecordSetPtr_t Data::Singleton::selectFiles(unsigned siteId, TimePoint_t tp)
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    _cache.siteId = siteId;
    _cache.timePoint = tp;

    Statement &stmt = tp ? _selectHistory : _selectTrunk;
    // Return null object if selected rows count is 0
    return RecordSetPtr_t(stmt.execute() ? new RecordSet(stmt) : 0);
}

Data::Singleton::RecordSetPtr_t Data::Singleton::selectIgnores(unsigned siteId)
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    _cache.siteId = siteId;

    return RecordSetPtr_t(
        _selectIgnores.execute() ? new RecordSet(_selectIgnores) : 0);
}

void Data::Singleton::addFile(unsigned siteId, const File& file)
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    bindCache(siteId, file);

    _insFile.execute();
    _cache.fileId = Poco::AnyCast<Poco::UInt64>(static_cast<SessionImpl*>(_ses.impl())->getInsertId(""));
    _cache.fileStatus = File::Added;
    _insHistory.execute();
}

void Data::Singleton::updFile(unsigned siteId, const File& file)
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    bindCache(siteId, file);

    _cache.fileStatus = File::Modified;
    _insHistory.execute();
    _updFile.execute();
}

void Data::Singleton::delFile(unsigned siteId, const File& file)
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    bindCache(siteId, file);

    _cache.fileStatus = File::Deleted;
    _cache.fileModifyDate.clear(); // additional information to recognize deleted files
    _insHistory.execute();
    _updFile.execute();
}


void Data::Singleton::incrementUsage()
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    ++_counter;
}

bool Data::Singleton::decrementUsage()
{
    Poco::FastMutex::ScopedLock lock(_mutex);
    if (!_counter) return true;
    return 0 == --_counter;
}

Data::Singleton &Data::Singleton::getInstance()
{
    ASSERT_THROW(0 != _singleton)
    return *_singleton;
}

void Data::Singleton::bindCache(unsigned siteId, const File& file)
{
    _cache.siteId = siteId;
    _cache.fileId = file.id;
    _cache.fileCrc32 = file.crc32;
    _cache.fileFullName = file.fullName;
    _cache.fileIsDirectory = file.isDirectory;
    _cache.fileModifyDate = file.modifyDate;
    _cache.timePoint = Data::currentTimePoint();
}

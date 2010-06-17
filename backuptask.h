#ifndef BACKUPTASK_H
#define BACKUPTASK_H

#include "data.h"
#include <list>
#include <set>
#include <Poco/Any.h>
#include <Poco/Task.h>

typedef std::vector<std::string> StrList_t;
typedef Poco::SharedPtr<StrList_t> StrListPtr_t;

class BackupTask : public Poco::Task
{
public:
    BackupTask(Data::Site::Ptr_t site, StrListPtr_t batch);
    ~BackupTask();

    void runTask();

    static void restore(Data::Site::Ptr_t site, Poco::DateTime dt);

private:
    bool processBatch();

    typedef std::list<Data::File::Ptr_t> Listing_t;
    void listFtpFiles(Listing_t& files, const std::string& path = "",
                      bool stopOnFail = false);
    Listing_t makeBufferMLSD(const std::string& path);
    Listing_t makeBufferDefault(const std::string& path);

    bool testIgnore(Data::Ignore::Attribute attr, const std::string& value);

    void writeLog(const std::string& msg);
    void writeLog(const std::string& msg, const Poco::Any& arg);

    void reconnect();
    
    static std::string backupDir();

private:
    class FtpClient;
    FtpClient *_ftp;

    Data::Site::Ptr_t _site;
    std::vector<std::set<std::string> > _ignoreOperands;
    StrListPtr_t _batch;
    std::string _timePoint;
};

#endif // BACKUPTASK_H

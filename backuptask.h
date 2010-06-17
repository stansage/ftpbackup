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

    typedef std::list<Data::File::Ptr_t> Output_t;
    void listFtpFiles(Output_t& files, const std::string& path = "",
                      bool stopOnFail = false);
    void makeBufferMLSD(const std::string& path);
    void makeBufferDefault(const std::string& path);

    bool testIgnore(Data::Ignore::Attribute attr, const std::string& value);

    void writeLog(const std::string& msg);
    void writeLog(const std::string& msg, const Poco::Any& arg);

    static std::string backupDir();

private:
    class FtpClient;
    FtpClient *_ftp;

    Data::Site::Ptr_t _site;
    std::vector<std::set<std::string> > _ignoreOperands;
    StrListPtr_t _batch;
    Data::File::List_t _buffer;
    std::string _timePoint;
};

#endif // BACKUPTASK_H

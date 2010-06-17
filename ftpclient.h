#ifndef FTPCLIENT_H
#define FTPCLIENT_H

#include "backuptask.h"
#include <Poco/Net/FTPClientSession.h>
#include <Poco/Net/SocketStream.h>

class BackupTask::FtpClient : public Poco::Net::FTPClientSession
{
public:
    void login(const std::string& user, const std::string& pass);

    enum Feature { MLSD, MDTM, FeatureCount };
    bool hasFeature(Feature feature);

    std::istream& beginMLSD(const std::string& path = "");
    void endMLSD();

    // Return crc32 of downloaded file content
    unsigned download(const std::string& src, const std::string& dst);
    // Recursively upload files
    void upload(const std::string& src);
    // Recursively remove files
    void removeAll(const std::string& path);

    static FtpClient *createConnect();

private:
    FtpClient(const std::string& host, Poco::UInt16 port);

private:
    Poco::Net::SocketStream**  _parentData;
    std::vector<bool> _features;
};

#endif // FTPCLIENT_H

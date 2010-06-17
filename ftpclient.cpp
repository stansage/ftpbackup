#include "ftpclient.h"
#include "main.h"

#include <memory.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Checksum.h>
#include <Poco/NumberParser.h>
#include <Poco/StringTokenizer.h>
#include <Poco/DirectoryIterator.h>
#include <Poco/Net/SocketStream.h>
#include <Poco/Net/FTPClientSession.h>

using Poco::Net::SocketStream;
using Poco::Net::FTPClientSession;

BackupTask::FtpClient::FtpClient(const std::string& host, Poco::UInt16 port) :
    FTPClientSession(host, port), _parentData(0)
{
}

void BackupTask::FtpClient::login(const std::string& user, const std::string& pass)
{
    FTPClientSession::login(user, pass);
    if (_parentData) return; // initialize pointer after authorization

    // Perform hardcore hacking
    SocketStream* parent = dynamic_cast<SocketStream*>(&beginList(".."));
    // find FTPClientSession::SocketStream* member position
    void* p = memmem(this, sizeof(FTPClientSession), &parent, sizeof(void*));
    poco_assert(0 != p);
    // Assign pointer _parentData to pointer FTPClientSession::_pDataStream
    _parentData = reinterpret_cast<SocketStream**>(p);
    // Check is pointer valid
    poco_assert(0 != *_parentData);
    endList();
    // Transfer ends and data stream released
    poco_assert(0 == *_parentData);

}

bool BackupTask::FtpClient::hasFeature(Feature feature)
{
    if (_features.empty()) {
        std::string response;
        std::vector<std::string> commands(FeatureCount);
        commands[MLSD] = "MLSD";
        commands[MDTM] = "MDTM";

        sendCommand("FEAT", response);
        _features.resize(FeatureCount);
        for (size_t i = 0, count = _features.size(); i < count; ++i)
            _features[i] = !commands[i].empty() &&
                           std::string::npos != response.find(commands[i]);
    }
    return _features[feature];
}

std::istream& BackupTask::FtpClient::beginMLSD(const std::string& path)
{
    if (!_parentData) return beginList(path);
    // Same implementation as FTPClientSession::beginList
    delete *_parentData;
    *_parentData = 0;
    *_parentData = new SocketStream(establishDataConnection("MLSD", path));
    return **_parentData;
}

void BackupTask::FtpClient::endMLSD()
{
    endTransfer();
}

unsigned BackupTask::FtpClient::download(const std::string& src, const std::string& dst)
{
    Poco::File(Poco::Path(dst).parent()).createDirectories();

    Poco::Checksum crc32(Poco::Checksum::TYPE_CRC32);
    Poco::FileOutputStream fstream(dst, std::ios::out | std::ios::trunc | std::ios::binary);

    // Read each byte and calculate checksum
    std::istream& data = beginDownload(src);
    for (char byte = data.get(); !data.eof(); byte = data.get()) {
        crc32.update(byte);
        fstream << byte;
    }
    endDownload();
    return crc32.checksum();
}

void BackupTask::FtpClient::upload(const std::string& src)
{
    std::string dst = App::lastToken(src, Poco::Path::separator());
    if (!Poco::File(src).isDirectory())
    { // upload one file
        Poco::FileInputStream fstream(src);
        std::ostream& data = beginUpload(dst);
        data << fstream.rdbuf();
        endUpload();
    } else { // just create new directory and recursively upload to it
        createDirectory(dst);
        setWorkingDirectory(dst);
        for (Poco::DirectoryIterator dit(src); !dit.name().empty(); ++dit)
            upload(dit.path().toString());
        cdup();
    }
}

void BackupTask::FtpClient::removeAll(const std::string& path)
{
    try { setWorkingDirectory(path); }  // if path not directory remove file and exit
    catch (...) { try { remove(path); } catch (...) { } return; }

    // list all files exclude '.'. and '..'
    std::string fname;
    std::list<std::string> fnames;
    std::istream& istream = beginList();
    while (std::getline(istream, fname)) {
        char last = fname[fname.size() - 1];
        if ('\n' == last || '\r' == last)
            fname.resize(fname.size() - 1);
        if (('.' == fname[0]) &&  // line == "." || line == ".."
            ((1 == fname.size()) ||
                (2 == fname.size() &&
                    '.' == fname[1]))) continue;
        fnames.push_back(fname);
    }
    endList();

    // Recursively remove current directory content
    for (std::list<std::string>::const_iterator it = fnames.begin(), end = fnames.end(); it != end; ++it)
        removeAll(*it);

    cdup();
    removeDirectory(path); // now remove empty dir
}

BackupTask::FtpClient *BackupTask::FtpClient::createConnect()
{
    static std::string host;
    static Poco::UInt16 port = 0;
    static int timeout = 0;
    static Poco::FastMutex mutex;

    if (!port) { // parse properties once
        Poco::FastMutex::ScopedLock lock(mutex); // lock to another threads
        Poco::StringTokenizer tok(App::config("ftp.connection"), ":");
        if (0 == tok.count())
            throw Poco::ApplicationException("Invalid ftp config property");
        host = tok[0];
        port = 2 == tok.count() ? Poco::NumberParser::parse(tok[1]) : FTPClientSession::FTP_PORT;

        timeout = Poco::NumberParser::parse(App::config("ftp.timeout", "0"));
    }

    FtpClient *ret = new FtpClient(host, port);
    if (timeout)
        ret->setTimeout(Poco::Timespan(timeout, 0));
    return ret;
}

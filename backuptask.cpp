#include "backuptask.h"
#include "ftpclient.h"
#include "main.h"

#include <memory>
//#include <ctime>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Format.h>
#include <Poco/NumberParser.h>
#include <Poco/StringTokenizer.h>
#include <Poco/DateTimeFormat.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Net/NetException.h>


BackupTask::BackupTask(Data::Site::Ptr_t site, StrListPtr_t batch) :
    Task("BackupTask"), _ftp(0), _site(site), _batch(batch),
    _timePoint(Poco::format("%?u", Data::currentTimePoint()))
{
    ASSERT_LOG(0 != site.get())
    writeLog("Connecting to ftp server");
    reconnect();
}

BackupTask::~BackupTask()
{
    delete _ftp;
}

void BackupTask::runTask()
{
    try {
        if (processBatch()) return;

        // Initialize ignores set
        _ignoreOperands.clear();
        _ignoreOperands.resize(Data::Ignore::CountOfAttributes);
        Data::Ignore::List_t ignores = _site->ignores();
        for (size_t i = 0, count = ignores.size(); i < count; ++i) {
            Data::Ignore::Ptr_t ign = ignores[i];
            if (ign->isValid())
                _ignoreOperands[ign->attribute].insert(ign->operand);
        }

        // Retrieve file list from ftp server
        Listing_t ftpFiles;
        listFtpFiles(ftpFiles);

        // Retrieve file list from database
        Data::File::List_t siteFiles = _site->files(); // retrieve db stored list
        writeLog("List files complete, found %z items", ftpFiles.size());

        // Create map: file_fullName, siteFiles_index, ftpFiles_exists
        typedef std::map<std::string, std::pair<size_t, bool> > SearchMap_t;
        SearchMap_t search; // search by name
        for (size_t i = 0, count = siteFiles.size(); i < count; ++i)
            search.insert(std::make_pair(siteFiles[i]->fullName, std::make_pair(i, false)));

        // Prepare working directory
        Poco::File workdir(Poco::format("%s/%u/%s", backupDir(), _site->id, _timePoint));
        if (workdir.exists()) workdir.remove(true);
        workdir.createDirectories(); // create work directory

        // Enumerate ftpFiles
        bool hasFiles = false;
        SearchMap_t::const_iterator send = search.end();
        for (Listing_t::iterator it = ftpFiles.begin(), end = ftpFiles.end(); it != end; ++it)
        {
            Data::File::Ptr_t ftpFile = *it;
            SearchMap_t::iterator sit = search.find(ftpFile->fullName);
            Poco::File fs(Poco::format("%s/%s", workdir.path(), ftpFile->fullName));
            try {

                if (send == sit) { // check existense in db list
                    writeLog("New entry discovered " + ftpFile->fullName);
                    // Daownload only real files
                    if (!it->get()->isDirectory)
                        it->get()->crc32 = _ftp->download(ftpFile->fullName, fs.path());
                    ftpFile->setStatus(Data::File::Added);
                    hasFiles = true;
                } else {
                    Data::File::Ptr_t siteFile = siteFiles[sit->second.first];
                    ftpFile->id = siteFile->id;
                    if (siteFile->isDirectory != ftpFile->isDirectory) {
                        writeLog(ftpFile->fullName + " type changed to " +
                            (ftpFile->isDirectory ? "directory" : "file"));
                        // Daownload only real files
                        if (!ftpFile->isDirectory)
                            ftpFile->crc32 = _ftp->download(ftpFile->fullName, fs.path());
                        ftpFile->setStatus(Data::File::Modified);
                        hasFiles = true;
                    } else if (!siteFile->isDirectory && siteFile->modifyDate != ftpFile->modifyDate) {
                        writeLog("Modify date is different for file " + ftpFile->fullName);
                        // Download it, because modifyDate checked for real files allways
                        ftpFile->crc32 = _ftp->download(ftpFile->fullName, fs.path());
                        // Second check for mdifycation by content checksum
                        if (siteFile->crc32 == ftpFile->crc32)
                            fs.remove(); // skip identical files
                        else
                            ftpFile->setStatus(Data::File::Modified);
                        hasFiles = true;
                    }
                }
            } catch (Poco::Exception& ex) {
                App::logger().error(
                    Poco::format("Error while processing file %s\n%s", ftpFile->fullName, ex.displayText()));
                if (fs.exists()) fs.remove(); // delete file on any error occured
            }

            sit->second.second = true; // mark that file has been processed
        }

        bool hasChanges = false;
        // All unmarked items will be saved as deleted
        for (SearchMap_t::const_iterator it = search.begin(); it != send; ++it)
        {
            if (it->second.second) continue; // file exists in ftp list
            Data::File::Ptr_t siteFile = siteFiles[it->second.first];
            writeLog("Entry has been deleted " + siteFile->fullName);
            siteFile->setStatus(Data::File::Deleted);
            hasChanges = true;
        }

        if (!hasFiles && !hasChanges)
            writeLog("All files up to date");
        else if (hasFiles) {
            writeLog("Creating archive " + workdir.path());
            int result = system(Poco::format("tar --directory=\"%s\" -czf \"%s.tar.gz\" ./",
                workdir.path(), workdir.path()).c_str());
            if (result)
                throw Poco::ApplicationException("tar failed", result);
        }
        workdir.remove(true);

    } catch (Poco::Exception& ex) {
        App::logger().log(ex);
    }
}

void BackupTask::restore(Data::Site::Ptr_t site, Poco::DateTime dt)
{
    ASSERT_LOG(0 != site.get())
    App::logger().information(Poco::format("Start restoring site %u on %s",
        site->id, Poco::DateTimeFormatter::format(dt, Poco::DateTimeFormat::SORTABLE_FORMAT)));

    // Convert local dt to UTC datetime
    std::time_t now = Poco::Timestamp().epochTime();
    dt.makeUTC(localtime(&now)->tm_gmtoff);
    Data::TimePoint_t timePoint = dt.timestamp().epochMicroseconds();
    Data::File::List_t siteFiles = site->files(timePoint);
    if (siteFiles.empty()) {
        App::logger().information(Poco::format("No archives found on specified timepoint %?u", timePoint));
        return;
    }

    // Create map File::fullName => (File::modifyDate, i)
    // Store here only files with greatest File::modifyDate values
    typedef std::map<std::string, std::pair<Data::TimePoint_t, size_t> > Files_t;
    Files_t files;
    for (size_t i = 0, count = siteFiles.size(); i < count; ++i) {
        Data::File::Ptr_t file = siteFiles[i];
        Data::TimePoint_t tp = Poco::NumberParser::parse64(file->modifyDate);
        Files_t::iterator it = files.find(file->fullName);

        if (files.end() == it)
            files[file->fullName] = std::make_pair(tp, i);
        else if (it->second.first < tp) {
            it->second.first = tp;
            it->second.second = i;
        }
    }

    // Group files by archive name (File::modifyDate) and skip deleted
    typedef std::map<Data::TimePoint_t, Listing_t> Archives_t;
    Archives_t archives;
    size_t total = 0; // files count minus skipped items
    for (Files_t::const_iterator it = files.begin(), end = files.end(); it != end; ++it)
    {
        Data::File::Ptr_t file = siteFiles[it->second.second];
        if (file->isDeleted()) continue; // skip deleted files
        archives[it->second.first].push_back(file);
        ++total;
    }

    // Prepare working directory
    std::string bdir = backupDir();
    Poco::File workdir(Poco::format("%s/%u-%?u", bdir, site->id, timePoint));
    if (workdir.exists()) workdir.remove(true);
    workdir.createDirectories();

    // Extract files to workdir
    App::logger().information(Poco::format("Extracting %z files from %z archives", total, archives.size()));
    for (Archives_t::const_iterator ait = archives.begin(), aend = archives.end(); ait != aend; ++ait)
    {
        // List files to be extracted
        Poco::File flist(Poco::format("%s/%?u.flist", bdir, ait->first));
        Poco::FileOutputStream fos(flist.path()); // save list of fullNames to file on disk
        for (Listing_t::const_iterator fit = ait->second.begin(), fend = ait->second.end(); fit != fend; ++fit)
        {
            Data::File::Ptr_t file = *fit;
            if (!file->isDirectory)
                fos << '.' << file->fullName << std::endl;
            else
                Poco::File(workdir.path() + file->fullName).createDirectories();
        }
        fos.close();

        if (0 != flist.getSize()) {
            int result = system(Poco::format("tar --directory=\"%s\" --files-from=\"%s\" -xf \"%s/%u/%?u.tar.gz\"",
                            workdir.path(), flist.path(), bdir, site->id, ait->first).c_str());
            if (result)
                throw Poco::ApplicationException("tar exit with error, aborting...", result);
        }
                    ;
        flist.remove();
    }

    Poco::Path dstpath(App::config("restore.path"));
    App::logger().information("Uploading to ftp " + dstpath.toString());

    std::auto_ptr<FtpClient> ftp(FtpClient::createConnect());
    ftp->login(site->login, site->password);

    for (int i = 0, count = dstpath.depth(); i < count; ++i)
    {
        const std::string& dir = dstpath[i];
        try { ftp->createDirectory(dir); } catch (...) { }
        ftp->setWorkingDirectory(dir);
    }
    ftp->removeAll(App::lastToken(workdir.path(), Poco::Path::separator()));
    ftp->upload(workdir.path());
    workdir.remove(true);
}

bool BackupTask::processBatch()
{
    if (!_batch) return false;

    writeLog("Processing batch commands");
    for (size_t i = 0, count = _batch->size(); i < count; ++i) {
        Poco::StringTokenizer tok(_batch->at(i), ":", Poco::StringTokenizer::TOK_TRIM | Poco::StringTokenizer::TOK_IGNORE_EMPTY);
        std::string response;
        if (tok.count() > 1) {
            writeLog("Command: " + tok[0] + ", Argument: " + tok[0]);
            _ftp->sendCommand(tok[0], tok[1], response);
        }
        else {
            writeLog("Command: " + tok[0]);
            _ftp->sendCommand(tok[0], response);
        }
        writeLog("Response: " + response);
    }

    return true;
}

void BackupTask::listFtpFiles(Listing_t& files, const std::string& path, bool stopOnFail)
{
    if (testIgnore(Data::Ignore::AttributePath, path))
        return; // skip directory by full path

    try
        {
        // Some ftp servers do not support full path
        if (path.empty())
            writeLog("Checking features");
        else { // on enter change work dir
            writeLog("List directory " + path);
            _ftp->setWorkingDirectory(App::lastToken(path, Poco::Path::separator()));
        }

        // Fill buffer implement two modes
        Listing_t listing = _ftp->hasFeature(FtpClient::MLSD)
                           ? makeBufferMLSD(path) : makeBufferDefault(path);

        // Recursively list all directories
        for (Listing_t::iterator it = listing.begin(), end = listing.end(); it != end; ++it)
        {
            Data::File::Ptr_t file = *it;
            if (file->fullName.empty()) {
                App::logger().warning("File has empty name, why!?");
                continue;
            }
            files.push_back(file);
            if (file->isDirectory)
                listFtpFiles(files, file->fullName);
            else
                writeLog("File found " + file->fullName);
        }

        if (!path.empty()) // on exit restore previous
            _ftp->cdup();
    } catch (Poco::Net::FTPException& ex) {
        if (stopOnFail) ex.rethrow();

        writeLog("Trying reconnect on FTPException " + ex.displayText());
        reconnect();
        Poco::Path dir(path); // restore current work path
        for (int i = 0, count = dir.depth(); i < count; ++i)
            _ftp->setWorkingDirectory(dir[i]);
        listFtpFiles(files, path, true); // once recursive call
    }
}

BackupTask::Listing_t BackupTask::makeBufferMLSD(const std::string& path)
{
    if (path.empty()) writeLog("Enabled MLSD mode");
    Listing_t ret;

    std::string line;
    std::istream& istream = _ftp->beginMLSD();
    while (std::getline(istream, line)) {
        Poco::StringTokenizer tok(line, ";", Poco::StringTokenizer::TOK_TRIM);
        if (!tok.count()) continue;
        // File name allways last token
        const std::string& fname = tok[tok.count() - 1];
        if (('.' == fname[0]) &&  // fname == "." || fname == ".."
            ((1 == fname.size()) ||
                (2 == fname.size() &&
                    '.' == fname[1]))) continue;
        if (testIgnore(Data::Ignore::AttributeExt, App::lastToken(fname, '.')))
            continue;

        // Construct file attributes map from first tok.count() - 1 tokens
        std::map<std::string, std::string> keyValue;
        for (size_t i = 0, count = tok.count() - 1; i < count; ++i) {
            const std::string& kv = tok[i];
            const size_t pos = kv.find_first_of('=', 0);
            if (std::string::npos == pos) continue;
            keyValue[kv.substr(0, pos)] = kv.substr(pos + 1, kv.size() - pos);
        }
        const std::string& type = keyValue["type"];
        if ("pdir" == type || "cdir" == type)
            continue; // allready skipped above

        ret.push_back(_site->createFile(
            path + Poco::Path::separator() + fname,
            keyValue["modify"], "dir" == type));
    }
    _ftp->endMLSD();
    return ret;
}

BackupTask::Listing_t BackupTask::makeBufferDefault(const std::string& path)
{
    if (path.empty()) writeLog("Enabled LIST mode");
    Listing_t ret;

    std::string line;
    std::istream& istream = _ftp->beginList();
    while (std::getline(istream, line)) {
        if (line.empty() || (('.' == line[0]) && ((1 == line.size())
            || (2 == line.size() && '.' == line[1]))))
            continue; // line = "" || line == "." || line == ".."
        if ('\r' == *line.rbegin() || '\n' == *line.rbegin())
            line.resize(line.size() - 1); // trim end
        if (testIgnore(Data::Ignore::AttributeExt, App::lastToken(line, '.')))
            continue; // skip file by extension

        // Create empty entries
        ret.push_back(_site->createFile(
            path + Poco::Path::separator() + line, _timePoint, false));
    }
    _ftp->endList();

    // REtrive attributes
    for (Listing_t::iterator it = ret.begin(), end = ret.end(); it != end; ++it)
    {
        Data::File::Ptr_t file = *it;
        line = App::lastToken(file->fullName, Poco::Path::separator());

        try { // if not dir then exception throws
            _ftp->setWorkingDirectory(line);
            file->isDirectory = true;
            _ftp->cdup();
        } catch (...) { }

        // Get modify date attribute if availible
        if (!file->isDirectory && _ftp->hasFeature(FtpClient::MDTM))
            _ftp->sendCommand("MDTM", line, file->modifyDate);
    }
    return ret;
}

bool BackupTask::testIgnore(Data::Ignore::Attribute attr, const std::string& value)
{
    if (attr < 0 || (size_t)attr >= _ignoreOperands.size()) return false;
    const std::set<std::string>& operands = _ignoreOperands[attr];
    return operands.find(value) != operands.end();
}

void BackupTask::writeLog(const std::string& msg)
{
    App::logger().information(Poco::format("Site(%u) " + msg, _site->id));
}

void BackupTask::writeLog(const std::string& msg, const Poco::Any& arg)
{
    App::logger().information(Poco::format("Site(%u) " + msg, _site->id, arg));
}

void BackupTask::reconnect()
{
    _ftp = FtpClient::createConnect();
    _ftp->login(_site->login, _site->password);
}

std::string BackupTask::backupDir()
{
    return App::config("backup.path", "/var/tmp/" + App::get().commandName());
}

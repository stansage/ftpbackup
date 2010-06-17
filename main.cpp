#include "data.h"
#include "backuptask.h"
#include "main.h"

#include <iostream>
#include <Poco/Format.h>
#include <Poco/TaskManager.h>
#include <Poco/NumberParser.h>
#include <Poco/DateTimeParser.h>
#include <Poco/StringTokenizer.h>
#include <Poco/Util/Application.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/HelpFormatter.h>

using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::OptionCallback;
using Poco::Util::HelpFormatter;

const std::string App::EmptyString;

class Main : public Application
{
public:
    Main()
    {
        // Reset all option flags;
        for (int i = 0; i < AllOptions; ++i)
            _optionRequested[i] = false;
    }

    ~Main() { }

protected:
    enum OptionName { HelpOption, VersionOption, ConfigOption, RestoreOption, AllOptions };

    void initialize(Poco::Util::Application& self)
    {
        (void)self;
        // load default configuration files, if present and no option given
        if (!HasOption(HelpOption) && !HasOption(VersionOption) && !loadConfiguration())
           throw Poco::NotFoundException("Default configuration");

/*
       int tzdiff;
       _restore.first = 2;
       _restore.second = Poco::DateTimeParser::parse("2010.06.04 13:59:59", tzdiff);
       _optionRequested[RestoreOption] = true;
*/
     //  Application::initialize(self);
     //  if (!HasOption(HelpOption) && !HasOption(VersionOption))
     //      logger().information("Starting up");
    }

    void uninitialize()
    {
      //  if (!HasOption(HelpOption) && !HasOption(VersionOption))
      //      logger().information("Shutting down");

        Application::uninitialize();
    }

    bool HasOption(OptionName opt) const
    {
        if (AllOptions == opt)
        {
            bool ret = true;
            for (int i = 0; i < AllOptions; ++i)
                ret &= _optionRequested[i];
            return ret;
        }
        return _optionRequested[opt];
    }

    void defineOptions(Poco::Util::OptionSet& options)
    {
        Application::defineOptions(options);

        options.addOption(Option("help", "h", "display this help")
            .callback(OptionCallback<Main>(this, &Main::handleHelp)));
        options.addOption(Option("version", "v", "display version information")
            .callback(OptionCallback<Main>(this, &Main::handleVersion)));
        options.addOption(Option("config", "c ", "load configuration data from a file")
            .argument("file")
            .callback(OptionCallback<Main>(this, &Main::handleConfig)));
        options.addOption(Option("restore", "r ", "restores archive on site up the date")
            .argument("id_site:datetime")
            .callback(OptionCallback<Main>(this, &Main::handleRestore)));
        options.addOption(Option("batch", "b ", "execute serial commands on ftp server, reserved words are: #quit, #continue")
            .argument("cmd1[:arg][,cmd2[:arg]]")
            .callback(OptionCallback<Main>(this, &Main::handleBatch)));
    }

    void handleHelp(const std::string& name, const std::string& value)
    {
        (void)name;
        (void)value;
        _optionRequested[HelpOption] = true;

        // Print usage
        HelpFormatter helpFormatter(options());
        helpFormatter.setCommand(commandName());
        helpFormatter.setUsage("OPTIONS");
        helpFormatter.setHeader("Backup and restore ftp files.");
        helpFormatter.format(std::cout);

        stopOptionsProcessing();
    }

    void handleVersion(const std::string& name, const std::string& value)
    {
        (void)name;
        (void)value;
        _optionRequested[VersionOption] = true;

        std::cout << "Ftp Backup (version 1.0.2)" << std::endl;
        stopOptionsProcessing();
    }

    void handleConfig(const std::string& name, const std::string& value)
    {
        (void)name;
        _optionRequested[ConfigOption] = true;

        loadConfiguration(value);
    }

    void handleRestore(const std::string& name, const std::string& value)
    {
        try {
            Poco::StringTokenizer tok(value, ",;",
                Poco::StringTokenizer::TOK_TRIM | Poco::StringTokenizer::TOK_IGNORE_EMPTY);
            if (2 != tok.count())
                throw Poco::ApplicationException("Check format (quotas used and delimeter match this ,;)");

            int tzdiff;
            _restore.first = Poco::NumberParser::parseUnsigned(tok[0]);
            _restore.second = Poco::DateTimeParser::parse(tok[1], tzdiff);

            _optionRequested[RestoreOption] = true;
        } catch (Poco::Exception& ex) {
            std::cout << "Can't recognize restore parameter \"" << value << "\"";
            std::cout << std::endl << ex.displayText() << std::endl << std::endl;
            handleHelp(name, value);
        }
    }

    void handleBatch(const std::string& name, const std::string& value)
    {
        if (value.empty())
            handleHelp(name, value);

        Poco::StringTokenizer tok(value, ",", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);
        _batch.assign(new StrList_t(tok.count()));
        _batch->assign(tok.begin(), tok.end());
    }

    int main(const std::vector<std::string>& args)
    {
        (void)args;
        if (HasOption(HelpOption) || HasOption(VersionOption))
            return EXIT_OK;

        Data data;
        if (HasOption(RestoreOption)) {
            Data::Site::Ptr_t site = data.siteById(_restore.first);
            if (!site) throw Poco::NotFoundException(Poco::format("Unable to find site with id %u", _restore.first));
            BackupTask::restore(site, _restore.second);
        } else {
            Poco::TaskManager tm;
            // Start observer thread
            for (size_t i = 0, count = data.sites().size(); i < count; ++i)
                try { tm.start(new BackupTask(data.sites()[i], _batch)); }
                catch (...) { }
            tm.joinAll();
        }
        return EXIT_OK;
    }

private:
    bool _optionRequested[AllOptions];
    std::pair<unsigned, Poco::DateTime> _restore;
    StrListPtr_t _batch;
};

POCO_APP_MAIN(Main)

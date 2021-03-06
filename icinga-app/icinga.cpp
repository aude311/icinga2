/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "config/configcompilercontext.h"
#include "config/configcompiler.h"
#include "base/application.h"
#include "base/logger_fwd.h"
#include "base/timer.h"
#include "base/utility.h"
#include <boost/program_options.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <boost/foreach.hpp>

#ifndef _WIN32
#	include "icinga-version.h"
#	define ICINGA_VERSION VERSION ", " GIT_MESSAGE

#	include <ltdl.h>
#endif /* _WIN32 */

using namespace icinga;
namespace po = boost::program_options;

static po::variables_map g_AppParams;

static bool LoadConfigFiles(bool validateOnly)
{
	ConfigCompilerContext::GetInstance()->Reset();

	BOOST_FOREACH(const String& configPath, g_AppParams["config"].as<std::vector<String> >()) {
		ConfigCompiler::CompileFile(configPath);
	}

	String name, fragment;
	BOOST_FOREACH(boost::tie(name, fragment), ConfigFragmentRegistry::GetInstance()->GetItems()) {
		ConfigCompiler::CompileText(name, fragment);
	}

	bool hasError = false;

	BOOST_FOREACH(const ConfigCompilerError& error, ConfigCompilerContext::GetInstance()->GetErrors()) {
		if (!error.Warning) {
			hasError = true;
			break;
		}
	}

	/* Don't link or validate if we have already encountered at least one error. */
	if (!hasError) {
		ConfigItem::LinkItems();
		ConfigItem::ValidateItems();
	}

	hasError = false;

	BOOST_FOREACH(const ConfigCompilerError& error, ConfigCompilerContext::GetInstance()->GetErrors()) {
		if (error.Warning) {
			Log(LogWarning, "icinga-app", "Config warning: " + error.Message);
		} else {
			hasError = true;
			Log(LogCritical, "icinga-app", "Config error: " + error.Message);
		}
	}

	if (hasError)
		return false;

	if (validateOnly)
		return true;

	ConfigItem::ActivateItems();

	ConfigItem::DiscardItems();
	ConfigType::DiscardTypes();

	return true;
}

static bool Daemonize(const String& stderrFile)
{
#ifndef _WIN32
	pid_t pid = fork();
	if (pid == -1) {
		return false;
	}

	if (pid)
		exit(0);

	int fdnull = open("/dev/null", O_RDWR);
	if (fdnull > 0) {
		if (fdnull != 0)
			dup2(fdnull, 0);

		if (fdnull != 1)
			dup2(fdnull, 1);

		if (fdnull > 2)
			close(fdnull);
	}

	const char *errPath = "/dev/null";

	if (!stderrFile.IsEmpty())
		errPath = stderrFile.CStr();

	int fderr = open(errPath, O_WRONLY | O_APPEND);

	if (fderr < 0 && errno == ENOENT)
		fderr = open(errPath, O_CREAT | O_WRONLY | O_APPEND, 0600);

	if (fderr > 0) {
		if (fderr != 2)
			dup2(fderr, 2);

		if (fderr > 2)
			close(fderr);
	}

	pid_t sid = setsid();
	if (sid == -1) {
		return false;
	}
#endif

	return true;
}

/**
 * Entry point for the Icinga application.
 *
 * @params argc Number of command line arguments.
 * @params argv Command line arguments.
 * @returns The application's exit status.
 */
int main(int argc, char **argv)
{
#ifndef _WIN32
	LTDL_SET_PRELOADED_SYMBOLS();
#endif /* _WIN32 */

#ifndef _WIN32
	lt_dlinit();
#endif /* _WIN32 */

	/* Set thread title. */
	Utility::SetThreadName("Main Thread");

	/* Set command-line arguments. */
	Application::SetArgC(argc);
	Application::SetArgV(argv);

	/* Install exception handlers to make debugging easier. */
	Application::InstallExceptionHandlers();

#ifdef ICINGA_PREFIX
	Application::SetPrefixDir(ICINGA_PREFIX);
#endif /* ICINGA_PREFIX */

#ifdef ICINGA_LOCALSTATEDIR
	Application::SetLocalStateDir(ICINGA_LOCALSTATEDIR);
#endif /* ICINGA_LOCALSTATEDIR */

#ifdef ICINGA_PKGLIBDIR
	Application::SetPkgLibDir(ICINGA_PKGLIBDIR);
#endif /* ICINGA_PKGLIBDIR */

#ifdef ICINGA_PKGDATADIR
	Application::SetPkgDataDir(ICINGA_PKGDATADIR);
#endif /* ICINGA_PKGDATADIR */

	po::options_description desc("Supported options");
	desc.add_options()
		("help", "show this help message")
		("version,V", "show version information")
		("library,l", po::value<std::vector<String> >(), "load a library")
		("include,I", po::value<std::vector<String> >(), "add include search directory")
		("config,c", po::value<std::vector<String> >(), "parse a configuration file")
		("validate,v", "exit after validating the configuration")
		("debug,x", "enable debugging")
		("daemonize,d", "detach from the controlling terminal")
		("errorlog,e", po::value<String>(), "log fatal errors to the specified log file (only works in combination with --daemonize)")
	;

	try {
		po::store(po::parse_command_line(argc, argv, desc), g_AppParams);
	} catch (const std::exception& ex) {
		std::ostringstream msgbuf;
		msgbuf << "Error while parsing command-line options: " << ex.what();
		Log(LogCritical, "icinga-app", msgbuf.str());
		return EXIT_FAILURE;
	}

	po::notify(g_AppParams);

	if (g_AppParams.count("debug"))
		Application::SetDebugging(true);

	if (g_AppParams.count("help") || g_AppParams.count("version")) {
		String appName = Utility::BaseName(argv[0]);

		if (appName.GetLength() > 3 && appName.SubStr(0, 3) == "lt-")
			appName = appName.SubStr(3, appName.GetLength() - 3);

		std::cout << appName << " " << "- The Icinga 2 network monitoring daemon.";

		if (g_AppParams.count("version")) {
#ifndef _WIN32
			std::cout  << " (Version: " << ICINGA_VERSION << ")";
#endif /* _WIN32 */
			std::cout << std::endl
				  << "Copyright (c) 2012-2013 Icinga Development Team (http://www.icinga.org)" << std::endl
				  << "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl2.html>" << std::endl
				  << "This is free software: you are free to change and redistribute it." << std::endl
				  << "There is NO WARRANTY, to the extent permitted by law.";
		}

		std::cout << std::endl;

		if (g_AppParams.count("version"))
			return EXIT_SUCCESS;
	}

	if (g_AppParams.count("help")) {
		std::cout << std::endl
			  << desc << std::endl
			  << "Report bugs at <https://dev.icinga.org/>" << std::endl
			  << "Icinga home page: <http://www.icinga.org/>" << std::endl;
		return EXIT_SUCCESS;
	}

	Log(LogInformation, "icinga-app", "Icinga application loader"
#ifndef _WIN32
	    " (version: " ICINGA_VERSION ")"
#endif /* _WIN32 */
	    );

	String searchDir = Application::GetPkgLibDir();
	Log(LogInformation, "base", "Adding library search dir: " + searchDir);

#ifdef _WIN32
	SetDllDirectory(searchDir.CStr());
#else /* _WIN32 */
	lt_dladdsearchdir(searchDir.CStr());
#endif /* _WIN32 */

	(void) Utility::LoadExtensionLibrary("icinga");

	if (g_AppParams.count("library")) {
		BOOST_FOREACH(const String& libraryName, g_AppParams["library"].as<std::vector<String> >()) {
			(void) Utility::LoadExtensionLibrary(libraryName);
		}
	}

	ConfigCompiler::AddIncludeSearchDir(Application::GetPkgDataDir());

	if (g_AppParams.count("include")) {
		BOOST_FOREACH(const String& includePath, g_AppParams["include"].as<std::vector<String> >()) {
			ConfigCompiler::AddIncludeSearchDir(includePath);
		}
	}

	if (g_AppParams.count("config") == 0) {
		Log(LogCritical, "icinga-app", "You need to specify at least one config file (using the --config option).");

		return EXIT_FAILURE;
	}

	if (g_AppParams.count("daemonize")) {
		String errorLog;

		if (g_AppParams.count("errorlog"))
			errorLog = g_AppParams["errorlog"].as<String>();

		Daemonize(errorLog);
	}

	bool validateOnly = g_AppParams.count("validate");

	if (!LoadConfigFiles(validateOnly))
		return EXIT_FAILURE;

	if (validateOnly) {
		Log(LogInformation, "icinga-app", "Finished validating the configuration file(s).");
		return EXIT_SUCCESS;
	}

	Application::Ptr app = Application::GetInstance();

	if (!app)
		BOOST_THROW_EXCEPTION(std::runtime_error("Configuration must create an Application object."));

	return app->Run();
}

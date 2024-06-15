//
//  main.cpp
//  interface/src
//
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QCommandLineParser>
#include <QtCore/QProcess>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLocalServer>
#include <QSharedMemory>
#include <QTranslator>

#include <BuildInfo.h>
#include <SandboxUtils.h>
#include <SharedUtil.h>
#include <NetworkAccessManager.h>
#include <gl/GLHelpers.h>

#include "AddressManager.h"
#include "Application.h"
#include "CrashHandler.h"
#include "InterfaceLogging.h"
#include "UserActivityLogger.h"
#include "MainWindow.h"

#include "Profile.h"

#ifdef Q_OS_WIN
#include <Windows.h>
extern "C" {
    typedef int(__stdcall * CHECKMINSPECPROC) ();
}
#endif

int main(int argc, const char* argv[]) {
#ifdef Q_OS_MAC
    auto format = getDefaultOpenGLSurfaceFormat();
    // Deal with some weirdness in the chromium context sharing on Mac.
    // The primary share context needs to be 3.2, so that the Chromium will
    // succeed in it's creation of it's command stub contexts.  
    format.setVersion(3, 2);
    // This appears to resolve the issues with corrupted fonts on OSX.  No
    // idea why.
    qputenv("QT_ENABLE_GLYPH_CACHE_WORKAROUND", "true");
	// https://i.kym-cdn.com/entries/icons/original/000/008/342/ihave.jpg
    QSurfaceFormat::setDefaultFormat(format);
#endif

#if defined(Q_OS_WIN) 
    // Check the minimum version of 
    if (gl::getAvailableVersion() < gl::getRequiredVersion()) {
        MessageBoxA(nullptr, "Interface requires OpenGL 4.1 or higher", "Unsupported", MB_OK);
        return -1;
    }
#endif

    setupHifiApplication(BuildInfo::INTERFACE_NAME);

    QStringList arguments;
    for (int i = 0; i < argc; ++i) {
        arguments << argv[i];
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Vircadia");
    QCommandLineOption versionOption = parser.addVersionOption();
    QCommandLineOption helpOption = parser.addHelpOption();

    QCommandLineOption urlOption("url", "", "value");
    QCommandLineOption noLauncherOption("no-launcher", "Do not execute the launcher");
    QCommandLineOption noUpdaterOption("no-updater", "Do not show auto-updater");
    QCommandLineOption checkMinSpecOption("checkMinSpec", "Check if machine meets minimum specifications");
    QCommandLineOption runServerOption("runServer", "Whether to run the server");
    QCommandLineOption serverContentPathOption("serverContentPath", "Where to find server content <path>", "serverContentPath");
    QCommandLineOption allowMultipleInstancesOption("allowMultipleInstances", "Allow multiple instances to run");
    QCommandLineOption overrideAppLocalDataPathOption("cache", "set test cache <dir>", "dir");
    QCommandLineOption overrideScriptsPathOption(SCRIPTS_SWITCH, "set scripts <path>", "path");
    QCommandLineOption responseTokensOption("tokens", "set response tokens <json>", "json");
    QCommandLineOption displayNameOption("displayName", "set user display name <string>", "string");
    QCommandLineOption setBookmarkOption("setBookmark", "set bookmark key=value pair", "string");
    QCommandLineOption defaultScriptOverrideOption("defaultScriptsOverride", "override defaultsScripts.js", "string");
    QCommandLineOption forceCrashReportingOption("forceCrashReporting", "Force crash reporting to initialize");

    parser.addOption(urlOption);
    parser.addOption(noLauncherOption);
    parser.addOption(noUpdaterOption);
    parser.addOption(checkMinSpecOption);
    parser.addOption(runServerOption);
    parser.addOption(serverContentPathOption);
    parser.addOption(overrideAppLocalDataPathOption);
    parser.addOption(overrideScriptsPathOption);
    parser.addOption(allowMultipleInstancesOption);
    parser.addOption(responseTokensOption);
    parser.addOption(displayNameOption);
    parser.addOption(setBookmarkOption);
    parser.addOption(defaultScriptOverrideOption);
    parser.addOption(forceCrashReportingOption);

    if (!parser.parse(arguments)) {
        std::cout << parser.errorText().toStdString() << std::endl; // Avoid Qt log spam
    }

    if (parser.isSet(versionOption)) {
        parser.showVersion();
        Q_UNREACHABLE();
    }
    if (parser.isSet(helpOption)) {
        QCoreApplication mockApp(argc, const_cast<char**>(argv)); // required for call to showHelp()
        parser.showHelp();
        Q_UNREACHABLE();
    }

    QString applicationPath;
    // A temporary application instance is needed to get the location of the running executable
    // Tests using high_resolution_clock show that this takes about 30-50 microseconds (on my machine, YMMV)
    // If we wanted to avoid the QCoreApplication, we would need to write our own
    // cross-platform implementation.
    {
        QCoreApplication tempApp(argc, const_cast<char**>(argv));
#ifdef Q_OS_OSX
        if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/../../../config.json")) {
            applicationPath = QCoreApplication::applicationDirPath() + "/../../../";
        } else {
            applicationPath = QCoreApplication::applicationDirPath();
        }
#else
        applicationPath = QCoreApplication::applicationDirPath();
#endif
    }
    static const QString APPLICATION_CONFIG_FILENAME = "config.json";
    QDir applicationDir(applicationPath);
    QString configFileName = applicationDir.filePath(APPLICATION_CONFIG_FILENAME);
    QFile configFile(configFileName);
    QString launcherPath;
    if (configFile.exists()) {
        if (!configFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Found application config, but could not open it";
        } else {
            auto contents = configFile.readAll();
            QJsonParseError error;

            auto doc = QJsonDocument::fromJson(contents, &error);
            if (error.error) {
                qWarning() << "Found application config, but could not parse it: " << error.errorString();
            } else {
                static const QString LAUNCHER_PATH_KEY = "launcherPath";
                launcherPath = doc.object()[LAUNCHER_PATH_KEY].toString();
                if (!launcherPath.isEmpty()) {
                    if (!parser.isSet(noLauncherOption)) {
                        qDebug() << "Found a launcherPath in application config. Starting launcher.";
                        QProcess launcher;
                        launcher.setProgram(launcherPath);
                        launcher.startDetached();
                        return 0;
                    } else {
                        qDebug() << "Found a launcherPath in application config, but the launcher"
                                    " has been suppressed. Continuing normal execution.";
                    }
                    configFile.close();
                }
            }
        }
    }

    // Early check for --traceFile argument 
    auto tracer = DependencyManager::set<tracing::Tracer>();
    const char * traceFile = nullptr;
    const QString traceFileFlag("--traceFile");
    float traceDuration = 0.0f;
    for (int a = 1; a < argc; ++a) {
        if (traceFileFlag == argv[a] && argc > a + 1) {
            traceFile = argv[a + 1];
            if (argc > a + 2) {
                traceDuration = atof(argv[a + 2]);
            }
            break;
        }
    }
    if (traceFile != nullptr) {
        tracer->startTracing();
    }
   
    PROFILE_SYNC_BEGIN(startup, "main startup", "");

#ifdef Q_OS_LINUX
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
#endif

#if defined(USE_GLES) && defined(Q_OS_WIN)
    // When using GLES on Windows, we can't create normal GL context in Qt, so 
    // we force Qt to use angle.  This will cause the QML to be unable to be used 
    // in the output window, so QML should be disabled.
    qputenv("QT_ANGLE_PLATFORM", "d3d11");
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
#endif

    QElapsedTimer startupTime;
    startupTime.start();

    Setting::init();

    // Instance UserActivityLogger now that the settings are loaded
    auto& ual = UserActivityLogger::getInstance();
    // once the settings have been loaded, check if we need to flip the default for UserActivityLogger
    if (!ual.isDisabledSettingSet()) {
        // the user activity logger is opt-out for Interface
        // but it's defaulted to disabled for other targets
        // so we need to enable it here if it has never been disabled by the user
        ual.disable(false);
    }
    qDebug() << "UserActivityLogger is enabled:" << ual.isEnabled();

    {
        bool isCrashHandlerEnabled = ual.isCrashMonitorEnabled() || parser.isSet(forceCrashReportingOption);
        qDebug() << "Crash handler logger is enabled:" << isCrashHandlerEnabled;
        if (isCrashHandlerEnabled) {
            auto crashHandlerStarted = startCrashHandler(argv[0]);
            qDebug() << "Crash handler started:" << crashHandlerStarted;
        }
    }

    const QString& applicationName = getInterfaceSharedMemoryName();
    bool instanceMightBeRunning = true;
#ifdef Q_OS_WIN
    // Try to create a shared memory block - if it can't be created, there is an instance of
    // interface already running. We only do this on Windows for now because of the potential
    // for crashed instances to leave behind shared memory instances on unix.
    QSharedMemory sharedMemory{ applicationName };
    instanceMightBeRunning = !sharedMemory.create(1, QSharedMemory::ReadOnly);
#endif

    // allow multiple interfaces to run if this environment variable is set.
    bool allowMultipleInstances = parser.isSet(allowMultipleInstancesOption) ||
                                  QProcessEnvironment::systemEnvironment().contains("HIFI_ALLOW_MULTIPLE_INSTANCES");
    if (allowMultipleInstances) {
        instanceMightBeRunning = false;
    }
    // this needs to be done here in main, as the mechanism for setting the
    // scripts directory appears not to work.  See the bug report
    // https://highfidelity.fogbugz.com/f/cases/5759/Issues-changing-scripts-directory-in-ScriptsEngine
    if (parser.isSet(overrideScriptsPathOption)) {
        QDir scriptsPath(parser.value(overrideScriptsPathOption));
        if (scriptsPath.exists()) {
            PathUtils::defaultScriptsLocation(scriptsPath.path());
        }
    }

    if (instanceMightBeRunning) {
        // Try to connect and send message to existing interface instance
        QLocalSocket socket;

        socket.connectToServer(applicationName);

        static const int LOCAL_SERVER_TIMEOUT_MS = 500;

        // Try to connect - if we can't connect, interface has probably just gone down
        if (socket.waitForConnected(LOCAL_SERVER_TIMEOUT_MS)) {
            if (parser.isSet(urlOption)) {
                QUrl url = QUrl(parser.value(urlOption));
                if (url.isValid() && (url.scheme() == URL_SCHEME_VIRCADIA || url.scheme() == URL_SCHEME_VIRCADIAAPP
                        || url.scheme() == HIFI_URL_SCHEME_HTTP || url.scheme() == HIFI_URL_SCHEME_HTTPS
                        || url.scheme() == HIFI_URL_SCHEME_FILE)) {
                    qDebug() << "Writing URL to local socket";
                    socket.write(url.toString().toUtf8());
                    if (!socket.waitForBytesWritten(5000)) {
                        qDebug() << "Error writing URL to local socket";
                    }
                }
            }

            socket.close();

            qDebug() << "Interface instance appears to be running, exiting";

            return EXIT_SUCCESS;
        }

#ifdef Q_OS_WIN
        return EXIT_SUCCESS;
#endif
    }


    // FIXME this method of checking the OpenGL version screws up the `QOpenGLContext::globalShareContext()` value, which in turn
    // leads to crashes when creating the real OpenGL instance.  Disabling for now until we come up with a better way of checking
    // the GL version on the system without resorting to creating a full Qt application
#if 0
    // Check OpenGL version.
    // This is done separately from the main Application so that start-up and shut-down logic within the main Application is
    // not made more complicated than it already is.
    bool overrideGLCheck = false;

    QJsonObject glData;
    {
        OpenGLVersionChecker openGLVersionChecker(argc, const_cast<char**>(argv));
        bool valid = true;
        glData = openGLVersionChecker.checkVersion(valid, overrideGLCheck);
        if (!valid) {
            if (overrideGLCheck) {
                auto glVersion = glData["version"].toString();
                qCWarning(interfaceapp, "Running on insufficient OpenGL version: %s.", glVersion.toStdString().c_str());
            } else {
                qCWarning(interfaceapp, "Early exit due to OpenGL version.");
                return 0;
            }
        }
    }
#endif


    // Debug option to demonstrate that the client's local time does not
    // need to be in sync with any other network node. This forces clock
    // skew for the individual client
    const char* CLOCK_SKEW = "--clockSkew";
    const char* clockSkewOption = getCmdOption(argc, argv, CLOCK_SKEW);
    if (clockSkewOption) {
        qint64 clockSkew = atoll(clockSkewOption);
        usecTimestampNowForceClockSkew(clockSkew);
        qCDebug(interfaceapp) << "clockSkewOption=" << clockSkewOption << "clockSkew=" << clockSkew;
    }

    // Oculus initialization MUST PRECEDE OpenGL context creation.
    // The nature of the Application constructor means this has to be either here,
    // or in the main window ctor, before GL startup.
    Application::initPlugins(arguments);

#ifdef Q_OS_WIN
    // If we're running in steam mode, we need to do an explicit check to ensure we're up to the required min spec
    if (parser.isSet(checkMinSpecOption)) {
        QString appPath;
        {
            char filename[MAX_PATH];
            GetModuleFileName(NULL, filename, MAX_PATH);
            QFileInfo appInfo(filename);
            appPath = appInfo.absolutePath();
        }
        QString openvrDllPath = appPath + "/plugins/openvr.dll";
        HMODULE openvrDll;
        CHECKMINSPECPROC checkMinSpecPtr;
        if ((openvrDll = LoadLibrary(openvrDllPath.toLocal8Bit().data())) &&
            (checkMinSpecPtr = (CHECKMINSPECPROC)GetProcAddress(openvrDll, "CheckMinSpec"))) {
            if (!checkMinSpecPtr()) {
                return -1;
            }
        }
    }
#endif

    int exitCode;
    {
        RunningMarker runningMarker(RUNNING_MARKER_FILENAME);
        bool runningMarkerExisted = runningMarker.fileExists();
        runningMarker.writeRunningMarkerFile();

        bool noUpdater = parser.isSet(noUpdaterOption);
        bool runServer = parser.isSet(runServerOption);
        bool serverContentPathOptionIsSet = parser.isSet(serverContentPathOption);
        QString serverContentPath = serverContentPathOptionIsSet ? parser.value(serverContentPathOption) : QString();
        if (runServer) {
            SandboxUtils::runLocalSandbox(serverContentPath, true, noUpdater);
        }

        // Extend argv to enable WebGL rendering
        std::vector<const char*> argvExtended(&argv[0], &argv[argc]);
        argvExtended.push_back("--ignore-gpu-blacklist");
#ifdef Q_OS_ANDROID
        argvExtended.push_back("--suppress-settings-reset");
#endif
        int argcExtended = (int)argvExtended.size();

        PROFILE_SYNC_END(startup, "main startup", "");
        PROFILE_SYNC_BEGIN(startup, "app full ctor", "");
        Application app(argcExtended, const_cast<char**>(argvExtended.data()), startupTime, runningMarkerExisted);
        PROFILE_SYNC_END(startup, "app full ctor", "");

#if defined(Q_OS_LINUX)
        app.setWindowIcon(QIcon(PathUtils::resourcesPath() + "images/vircadia-logo.svg"));
#endif
        startCrashHookMonitor(&app);

        QTimer exitTimer;
        if (traceDuration > 0.0f) {
            exitTimer.setSingleShot(true);
            QObject::connect(&exitTimer, &QTimer::timeout, &app, &Application::quit);
            exitTimer.start(int(1000 * traceDuration));
        }

#if 0
        // If we failed the OpenGLVersion check, log it.
        if (overrideGLcheck) {
            auto accountManager = DependencyManager::get<AccountManager>();
            if (accountManager->isLoggedIn()) {
                UserActivityLogger::getInstance().insufficientGLVersion(glData);
            } else {
                QObject::connect(accountManager.data(), &AccountManager::loginComplete, [glData](){
                    static bool loggedInsufficientGL = false;
                    if (!loggedInsufficientGL) {
                        UserActivityLogger::getInstance().insufficientGLVersion(glData);
                        loggedInsufficientGL = true;
                    }
                });
            }
        }
#endif

        // Setup local server
        QLocalServer server { &app };

        // We failed to connect to a local server, so we remove any existing servers.
        server.removeServer(applicationName);
        server.listen(applicationName);

        QObject::connect(&server, &QLocalServer::newConnection,
                         &app, &Application::handleLocalServerConnection, Qt::DirectConnection);

        printSystemInformation();

        auto appPointer = dynamic_cast<Application*>(&app);
        if (appPointer) {
            if (parser.isSet(urlOption)) {
                appPointer->overrideEntry();
            }
            if (parser.isSet(displayNameOption)) {
                QString displayName = QString(parser.value(displayNameOption));
                appPointer->forceDisplayName(displayName);
            }
            if (!launcherPath.isEmpty()) {
                appPointer->setConfigFileURL(configFileName);
            }
            if (parser.isSet(responseTokensOption)) {
                QString tokens = QString(parser.value(responseTokensOption));
                appPointer->forceLoginWithTokens(tokens);
            }
        }

        QTranslator translator;
        translator.load("i18n/interface_en");
        app.installTranslator(&translator);
        qCDebug(interfaceapp, "Created QT Application.");
        exitCode = app.exec();
        server.close();

        if (traceFile != nullptr) {
            tracer->stopTracing();
            tracer->serialize(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + traceFile);
        }
    }

    Application::shutdownPlugins();

    qCDebug(interfaceapp, "Normal exit.");
#if !defined(DEBUG) && !defined(Q_OS_LINUX)
    // HACK: exit immediately (don't handle shutdown callbacks) for Release build
    _exit(exitCode);
#endif
    return exitCode;
}

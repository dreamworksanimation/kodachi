// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/logging/plugin/KodachiLoggingPlugin.h>
#include <kodachi/plugin_system/KdPlugin.h>

#include <iostream>
#include <mutex>

namespace {

constexpr bool kSuppressDuplicatesDefault = true;

std::mutex sLogMutex;
thread_local KdThreadLogPoolHandle tLogPool = nullptr;

}

/**
 * Create a ThreadLogPool to aggregate the output of errors logged
 * on a given thread. While this exists on a thread (it's tracked
 * in a thread_local variable), logged errors will be held in a queue
 * until this object is destroyed. Then all errors will be sent to
 * the manager's sinks at once.
 *
 * There is limited support for having a pool for the non-default ErrorManager.
 * If you create a second pool on a thread that already has one, the outter
 * one should take precedence. (Two pools on the same thread for different
 * managers will therefore not really work.)
 */
class KodachiThreadLogPool
{
public:
    KodachiThreadLogPool(bool bracketWithInfoLines = true,
                         const std::string &blockDesc="");

    ~KodachiThreadLogPool();
private:
    struct LogEntry
    {
        LogEntry(std::string module,
                 std::string message,
                 KdLoggingSeverity severity)
            : mModule(std::move(module))
            , mMessage(std::move(message))
            , mSeverity(severity)
            {}

        std::string mModule;
        std::string mMessage;
        KdLoggingSeverity mSeverity;
    };

    friend kodachi::KodachiLoggingPlugin;
    void addLogEntry(LogEntry&& entry);

    std::vector<LogEntry> mEntries;
    std::string mBlockDescription;

    bool mBracket;
    KdLoggingSeverity mOurSeverity = kKdLoggingSeverityDebug;
    std::string mOurModule;
};

namespace kodachi {

static KdLoggingSeverity kSeverity = kKdLoggingSeverityError;


void
defaultLogHandler(const char* message,
                  KdLoggingSeverity severity,
                  const char* module,
                  const char* file,
                  int line,
                  int indent)
{
    std::string sevString;
    switch (severity)
    {
    case kKdLoggingSeverityDebug:
        sevString = "DEBUG";
        break;
    case kKdLoggingSeverityInfo:
        sevString = "INFO";
        break;
    case kKdLoggingSeverityWarning:
        sevString = "WARN";
        break;
    case kKdLoggingSeverityError:
        sevString = "ERROR";
        break;
    case kKdLoggingSeverityFatal:
        sevString = "FATAL";
        break;
    default:
        break;
    }

    for (uint i = 0; i < indent; ++i) {
        std::cerr << "    ";
    }
    std::cerr << module << " - " << sevString << ": " << message << "\n";
}


////////////////////////////////////////////////////
// C callbacks implementations for the plugin suite
////////////////////////////////////////////////////

struct KodachiLoggingPlugin::HandlerData
{
    HandlerData(KdLogHandler handler,
            void* context,
            KdLoggingSeverity severityThreshold,
            const char* module)
        : mHandler(handler)
        , mContext(context)
        , mSeverityThreshold(severityThreshold)
        , mModule(module)
        {}

    KdLogHandler mHandler;
    void* mContext;
    KdLoggingSeverity mSeverityThreshold;
    const char* mModule;
};

void
KodachiLoggingPlugin::log(const char* message,
                          KdLoggingSeverity severity,
                          const char* module,
                          const char* file,
                          int line)
{
    if (tLogPool) {
        tLogPool->addLogEntry(KodachiThreadLogPool::LogEntry(module, message, severity));
    } else {
        std::lock_guard<std::mutex> lock(sLogMutex);
        logInternal(message, severity, module, file, line);
    }
}


void
KodachiLoggingPlugin::logInternal(const char* message,
                                  KdLoggingSeverity severity,
                                  const char* module,
                                  const char* file,
                                  int line,
                                  int indent)
{
    if (mHandlers.empty()) {
        // let the default handler take care of everything
        defaultLogHandler(message, severity, module, file, line, indent);
        return;
    }

    // Loop through the handlers
    for (const auto& handlerData : mHandlers) {
        handlerData->mHandler(message,
                              severity,
                              module,
                              file,
                              line,
                              indent,
                              handlerData->mContext);
    }
}


void*
KodachiLoggingPlugin::registerHandler(KdLogHandler handler,
                             void* context,
                             KdLoggingSeverity severityThreshold,
                             const char* module)
{
    std::unique_ptr<HandlerData> tempHandler(new HandlerData(handler,
                                                             context,
                                                             severityThreshold,
                                                             module));
    mHandlers.push_back(std::move(tempHandler));

    return mHandlers.back().get();
}


int
KodachiLoggingPlugin::unregisterHandler(void* handlerToken)
{
    // Return true if and only if a handler was unregistered
    for (auto it = mHandlers.begin(); it != mHandlers.end(); ++it) {
        if ((*it).get() == handlerToken) {
            mHandlers.erase(it);
            return 1;
        }
    }

    return 0;
}


int
KodachiLoggingPlugin::isSeverityEnabled(const char* module,
                                        KdLoggingSeverity severity)
{
    // Get severity for the matching handler
    if (!mHandlers.empty()) {
        for (const auto& handler : mHandlers) {
            if (handler->mModule == module &&
                    severity >= handler->mSeverityThreshold) {
                return 1;
            }
        }
    }

    if (severity >= getSeverity()) {
        return 1;
    }

    return 0;
}


int
KodachiLoggingPlugin::getSeverity()
{
    return kSeverity;
}


void
KodachiLoggingPlugin::setSeverity(KdLoggingSeverity severity)
{
    kSeverity = severity;

    // loop through the handlers and set their severity
    for (const auto& handler : mHandlers) {
        if (handler->mSeverityThreshold != severity) {
            handler->mSeverityThreshold = severity;
        }
    }
}


KdPluginStatus
KodachiLoggingPlugin::setHost(KdPluginHost* host)
{
    sHost = host;

    return kodachi::PluginManager::setHost(host);
}


KdPluginHost*
KodachiLoggingPlugin::getHost()
{
    return sHost;
}


KodachiLoggingSuite_v1
KodachiLoggingPlugin::createSuite()
{
    KodachiLoggingSuite_v1 suite;

    suite.log = log;
    suite.registerHandler = registerHandler;
    suite.unregisterHandler = unregisterHandler;
    suite.isSeverityEnabled = isSeverityEnabled;
    suite.setSeverity = setSeverity;
    suite.getSeverity = getSeverity;
    suite.createThreadLogPool = createThreadLogPool;
    suite.releaseThreadLogPool = releaseThreadLogPool;

    return suite;
}

KdThreadLogPoolHandle
KodachiLoggingPlugin::createThreadLogPool(int bracket, const char* label)
{
    // The constructor will set itself as the thread_local pool if
    // it has to
    const bool useBracket = bracket ? true : false;
    KodachiThreadLogPool* pool = new KodachiThreadLogPool(useBracket, label);

    return pool;
}

void
KodachiLoggingPlugin::releaseThreadLogPool(KdThreadLogPoolHandle handle)
{
    if (tLogPool == handle) {
        delete handle;
    }
}

void
KodachiLoggingPlugin::flush()
{
    // TODO flush?
}

KdPluginHost* KodachiLoggingPlugin::sHost = nullptr;
std::vector<std::unique_ptr<KodachiLoggingPlugin::HandlerData>> KodachiLoggingPlugin::mHandlers;

} // namespace kodachi

//=============================================================================
// ThreadLogPool
//=============================================================================
KodachiThreadLogPool::KodachiThreadLogPool(bool bracketWithInfoLines,
                             const std::string &blockDesc)
    : mBlockDescription(blockDesc)
    , mBracket(bracketWithInfoLines)
{
    // only make us the local pool if there isn't one already
    if (!tLogPool) {
        tLogPool = this;
    }
}

KodachiThreadLogPool::~KodachiThreadLogPool()
{
    // remove us as the thread local pool if it's us
    if (tLogPool == this) {
        tLogPool = nullptr;
    }

    // If we had entries, even if we're not the local pool,
    // we want to log them.
    if (mEntries.empty()) {
        return; // early out to avoid empty brackets
    }

    // Lock manager so the whole block is coherent
    std::lock_guard<std::mutex> lock(sLogMutex);

    // Add start bracket if requested
    if (mBracket) {
        kodachi::KodachiLoggingPlugin::logInternal(std::string(mBlockDescription + " --->").c_str(),
                           mOurSeverity,
                           mOurModule.c_str(),
                           "",
                           0);
    }

    // Log accumulated errors, indenting if bracketed
    const std::uint32_t indent = mBracket ? 1 : 0;
    for (const auto &entry : mEntries) {
        kodachi::KodachiLoggingPlugin::logInternal(entry.mMessage.c_str(),
                           entry.mSeverity,
                           entry.mModule.c_str(),
                           "",
                           0,
                           indent);
    }

    // Add end bracket if requested
    if (mBracket) {
        kodachi::KodachiLoggingPlugin::logInternal("<---",
                           mOurSeverity,
                           mOurModule.c_str(),
                           "",
                           0);
    }
}


void
KodachiThreadLogPool::addLogEntry(KodachiThreadLogPool::LogEntry&& entry)
{
    mEntries.emplace_back(std::move(entry));

    // adjust our severity to match contents
    if (mEntries.back().mSeverity > mOurSeverity) {
        mOurSeverity = mEntries.back().mSeverity;
    }

    mOurModule = mEntries.back().mModule;
}

// Plugin registration
namespace {

using namespace kodachi;

kodachi::KdPlugin KodachiLoggingPlugin_plugin;
                                                                                                                                         \
KodachiLoggingSuite_v1 KodachiLoggingPlugin_suite =
        KodachiLoggingPlugin::createSuite();

const void* KodachiLoggingPlugin_getSuite()
{
    return &KodachiLoggingPlugin_suite;
}

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KodachiLoggingPlugin, "KodachiLogging", 0, 1);
}


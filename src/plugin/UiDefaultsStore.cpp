// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 界面全局默认值的存取：XML 属性文件加进程间锁，避让期用单调时钟计时。
#include "UiDefaultsStore.h"

#include <juce_data_structures/juce_data_structures.h>

#include <chrono>
#include <memory>

#include "BridgeBase.h"

namespace miciri::uidefaults
{
namespace
{
constexpr const char* kKeyLangChosen = "lang_chosen";
constexpr const char* kKeyLang = "lang";
constexpr const char* kKeyGuideSeen = "guide_seen";
constexpr const char* kKeyUiScale = "ui_scale_percent";

juce::File& testDirRef()
{
    static juce::File dir;
    return dir;
}

juce::InterProcessLock& processLock()
{
    static juce::InterProcessLock lock{kProcessLockName};
    return lock;
}

using MonotonicClock = std::chrono::steady_clock;
static_assert(MonotonicClock::is_steady, "避让窗口必须用单调时钟计时");

std::int64_t monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(MonotonicClock::now().time_since_epoch()).count();
}

struct LockBackoff
{
    bool active = false;
    std::int64_t sinceMs = 0;
};

LockBackoff& lockBackoff()
{
    static LockBackoff backoff;
    return backoff;
}

enum class Access
{
    read,
    write
};

// 本次访问该等多久；避让期满自动退出避让。inBackoff 返回访问开始时是否处在避让期。
int lockWaitMs(Access access, bool& inBackoff)
{
    auto& backoff = lockBackoff();
    if (backoff.active && monotonicMs() - backoff.sinceMs >= kProcessLockBackoffMs)
        backoff.active = false;
    inBackoff = backoff.active;
    if (inBackoff)
        return access == Access::read ? kProcessLockBackoffReadWaitMs : kProcessLockBackoffWriteWaitMs;
    return access == Access::read ? kProcessLockReadWaitMs : kProcessLockWriteWaitMs;
}

// 取到锁就退出避让；避让期外没取到就进入避让；避让期内再失败不续期。
void noteLockResult(bool held, bool wasInBackoff)
{
    auto& backoff = lockBackoff();
    if (held)
    {
        backoff.active = false;
    }
    else if (!wasInBackoff)
    {
        backoff.active = true;
        backoff.sinceMs = monotonicMs();
    }
}

std::unique_ptr<juce::PropertiesFile> openFile(juce::InterProcessLock* heldLock)
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    options.processLock = heldLock;
    const juce::File& testDir = testDirRef();
    if (testDir != juce::File())
        return std::make_unique<juce::PropertiesFile>(testDir.getChildFile("ui-defaults.settings"), options);
    options.applicationName = "ui-defaults";
    options.folderName = "Synchain/Miciri";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    return std::make_unique<juce::PropertiesFile>(options);
}

// 一次访问：取锁、打开文件，析构时先关文件再放锁。没取到锁时仍然尝试无锁打开，读不到就返回空指针。
class StoreSession
{
public:
    explicit StoreSession(Access access)
    {
        bool inBackoff = false;
        const int waitMs = lockWaitMs(access, inBackoff);
        held_ = processLock().enter(waitMs);
        noteLockResult(held_, inBackoff);
        file_ = openFile(held_ ? &processLock() : nullptr);
    }

    ~StoreSession()
    {
        file_.reset();
        if (held_)
            processLock().exit();
    }

    StoreSession(const StoreSession&) = delete;
    StoreSession& operator=(const StoreSession&) = delete;

    juce::PropertiesFile* file() const { return file_.get(); }

private:
    bool held_ = false;
    std::unique_ptr<juce::PropertiesFile> file_;
};
} // namespace

bool langChosenGlobal()
{
    const StoreSession session(Access::read);
    const auto* f = session.file();
    return f != nullptr && f->getBoolValue(kKeyLangChosen, false);
}

juce::String langGlobal()
{
    const StoreSession session(Access::read);
    const auto* f = session.file();
    if (f == nullptr)
        return {};
    const juce::String v = f->getValue(kKeyLang, juce::String());
    return bridge::isSupportedLang(v) ? v : juce::String();
}

void chooseLangGlobal(const juce::String& lang)
{
    if (!bridge::isSupportedLang(lang))
        return;
    const StoreSession session(Access::write);
    auto* f = session.file();
    if (f == nullptr)
        return;
    f->setValue(kKeyLang, lang);
    f->setValue(kKeyLangChosen, true);
    f->saveIfNeeded();
}

bool guideSeenGlobal()
{
    const StoreSession session(Access::read);
    const auto* f = session.file();
    return f != nullptr && f->getBoolValue(kKeyGuideSeen, false);
}

void setGuideSeenGlobal(bool seen)
{
    const StoreSession session(Access::write);
    auto* f = session.file();
    if (f == nullptr)
        return;
    f->setValue(kKeyGuideSeen, seen);
    f->saveIfNeeded();
}

int uiScalePercent()
{
    const StoreSession session(Access::read);
    const auto* f = session.file();
    if (f == nullptr)
        return 0;
    const int percent = f->getIntValue(kKeyUiScale, 0);
    return bridge::percentInRange(percent) ? percent : 0;
}

void setUiScalePercent(int percent)
{
    if (!bridge::percentInRange(percent))
        return;
    const StoreSession session(Access::write);
    auto* f = session.file();
    if (f == nullptr)
        return;
    f->setValue(kKeyUiScale, percent);
    f->saveIfNeeded();
}

void setStorageDirForTests(const juce::File& dir)
{
    testDirRef() = dir;
}
} // namespace miciri::uidefaults

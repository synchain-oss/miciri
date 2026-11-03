// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 网页编辑器宿主的实现。
#include "WebViewHost.h"

#include <string>
#include <type_traits>
#include <utility>

#include "PlatformWebView.h"
#include "ResourceProvider.h"
#include "UiDefaultsStore.h"

namespace miciri::webview
{
using WBC = juce::WebBrowserComponent;

namespace
{
constexpr const char* kFallbackTitle = "Miciri";

juce::String navStalledMessage()
{
    return "The page started loading but never finished.\n"
           "Click Retry, or close and reopen this plugin window.";
}

juce::String bridgeStalledMessage()
{
    return "The page loaded, but the plugin UI never finished starting up.\n"
           "Click Retry; if it keeps failing, report the diagnostics line below.";
}

juce::String bootErrorMessage()
{
    return "The plugin UI failed to start (front-end script error).\n"
           "Click Retry; if it keeps failing, report the diagnostics line below.";
}

juce::String pageMissingMessage()
{
    return "The UI page is missing from this build of the plugin.\n"
           "Reinstall the plugin; if it keeps failing, report the diagnostics line below.";
}

// 本进程里已经桥就绪的编辑器个数：大于零说明 WebView 环境已经热了，看门狗预算可以收紧。
std::atomic<int>& readyBridgeCount()
{
    static std::atomic<int> count{0};
    return count;
}

// 新窗口将出现的显示器可用区域：鼠标所在的屏幕，没有就用主屏。
juce::Rectangle<int> openAreaForNewWindow()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForPoint(juce::Desktop::getMousePosition());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    return display != nullptr ? display->userArea : juce::Rectangle<int>();
}

// 存过的语言优先；没存过就跟系统语言（中文用 zh，其余 en）。
juce::String initialLang()
{
    const auto saved = uidefaults::langGlobal();
    if (saved.isNotEmpty())
        return saved;
    return juce::SystemStats::getUserLanguage().startsWithIgnoreCase("zh") ? juce::String("zh") : juce::String("en");
}

// 页面上报的文字写进日志前的长度上限。
constexpr int kBootStageMaxChars = 40;
constexpr int kBootDetailMaxChars = 300;
constexpr int kNavErrorMaxChars = 200;
constexpr int kNavUrlLogMaxChars = 200;
} // namespace

// WebView 组件：拦截导航（只许资源根同源）、拦截新窗口，并把导航状态回报给宿主。
class WebViewHost::HostWebView final : public juce::WebBrowserComponent
{
public:
    HostWebView(WebViewHost& owner, Options options) : juce::WebBrowserComponent(std::move(options)), owner_(owner) {}

    // 底层会在每一帧用白色填底，必须覆盖 paint 并先画占位，否则开窗时会闪白。
    void paint(juce::Graphics& g) override
    {
        juce::WebBrowserComponent::paint(g);
        owner_.paintPlaceholder(g, getLocalBounds().toFloat());
    }

    // 窗口被遮挡闸挪走、没有可见区域时，WebView2 的控制器不会被创建；离屏画一次把它推起来。
    void pumpControllerCreation()
    {
        juce::Graphics g(pumpScratch_);
        juce::WebBrowserComponent::paint(g);
    }

    bool pageAboutToLoad(const juce::String& url) override
    {
        if (miciri::webview::ResourceProvider::isBlankPage(url))
            return true;
        if (!miciri::webview::ResourceProvider::isRootNavigation(url, WBC::getResourceProviderRoot()))
        {
            owner_.logDiag("blocked navigation: " + miciri::webview::ResourceProvider::urlForLog(url));
            return false;
        }
        owner_.onNavigationStarted(url);
        return true;
    }

    void pageFinishedLoading(const juce::String& url) override
    {
        if (miciri::webview::ResourceProvider::isBlankPage(url))
            return;
        owner_.onNavigationFinished(url);
    }

    void newWindowAttemptingToLoad(const juce::String& newURL) override
    {
        owner_.logDiag("blocked new-window request: " + miciri::webview::ResourceProvider::urlForLog(newURL.trim()));
    }

    // 返回 false：不显示内建错误页，兜底面板给的是可操作的信息。
    bool pageLoadHadNetworkError(const juce::String& errorInfo) override
    {
        owner_.onNavigationError(errorInfo);
        return false;
    }

    static_assert(std::is_same_v<decltype(&HostWebView::paint), void (HostWebView::*)(juce::Graphics&)>,
                  "HostWebView 必须自己覆盖 paint，否则底层的白底会让开窗闪白");

private:
    WebViewHost& owner_;
    juce::Image pumpScratch_{juce::Image::RGB, 1, 1, false, juce::SoftwareImageType()};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWebView)
};

WebViewHost::WebViewHost(juce::AudioProcessor& processor, Config config)
    : juce::AudioProcessorEditor(&processor), config_(std::move(config)), provider_(config_.resourceSource),
      scale_(bridge::scaleFromStoredPercent(uidefaults::uiScalePercent())), lang_(initialLang())
{
    setOpaque(true);

    const auto area = openAreaForNewWindow();
    scale_.fitToDisplay(area.getWidth(), area.getHeight());

    userDataFolder_ = PlatformWebView::makeUserDataFolder();
    userDataFolderIssue_ = PlatformWebView::probeUserDataFolder(userDataFolder_);
    if (userDataFolderIssue_.isNotEmpty())
        logDiag("user-data folder problem: " + userDataFolderIssue_ + " (" +
                PlatformWebView::userDataFolderDisplay(userDataFolder_, false) + ")");

    webView_ = std::make_unique<HostWebView>(*this, makeOptions());
    addAndMakeVisible(*webView_);

    setResizable(false, false); // 尺寸只经缩放档位编程改变，不开自由拖角
    resizeToDesignBox();
    beginLoadAttempt();
}

WebViewHost::~WebViewHost()
{
    stopTimer();
    logDiag("window closed after " + juce::String(static_cast<int>(juce::Time::getMillisecondCounter() - startMs_)) +
            " ms");
    releaseReadyBridge();
}

// 一次加载尝试的起点（构造与重试共用）：重置状态、探测运行时、检查入口页、开始导航。
void WebViewHost::beginLoadAttempt()
{
    releaseReadyBridge();
    bridgeReady_ = false;
    navState_ = NavState::notStarted;
    navDetail_ = {};
    bootError_ = {};
    navBudgetApplied_ = false;
    revealGate_.beginLoadAttempt();
    revealLogged_ = false;
    startMs_ = juce::Time::getMillisecondCounter();
    runtime_ = PlatformWebView::runtimeInfo();

    if (!isTimerRunning())
        startTimerHz(kTickHz);

    if (runtime_.status == PlatformWebView::RuntimeStatus::missing)
    {
        webView_->setVisible(false);
        showFallback(FallbackReason::MissingRuntime);
        return;
    }
    if (runtime_.status == PlatformWebView::RuntimeStatus::tooOld)
    {
        webView_->setVisible(false);
        showFallback(FallbackReason::RuntimeTooOld);
        return;
    }
    if (!provider_.lookup(entryUrl()).has_value())
    {
        webView_->setVisible(false);
        showFallback(FallbackReason::PageMissing);
        return;
    }

    const bool warm = readyBridgeCount().load() > 0;
    const int budget = warm ? kWarmLoadBudgetMs : kColdLoadBudgetMs;
    deadlineMs_ = startMs_ + static_cast<juce::uint32>(budget);
    logDiag(juce::String(warm ? "warm" : "cold") + " start, budget " + juce::String(budget) + " ms");

    const auto bgLine = PlatformWebView::backgroundColourDiagnostics(runtime_);
    if (bgLine.isNotEmpty())
        logDiag(bgLine);

    if (PlatformWebView::revealGateEnabled())
    {
        revealGate_.holdBeforeNavigation();
        applyRevealGate();
    }
    webView_->setVisible(true);
    webView_->goToURL(entryUrl());
}

juce::String WebViewHost::entryUrl() const
{
    return WBC::getResourceProviderRoot() + config_.entryPath;
}

void WebViewHost::releaseReadyBridge()
{
    if (countedAsReady_)
    {
        countedAsReady_ = false;
        readyBridgeCount().fetch_sub(1);
    }
}

void WebViewHost::paint(juce::Graphics& g)
{
    paintPlaceholder(g, getLocalBounds().toFloat());
}

void WebViewHost::paintPlaceholder(juce::Graphics& g, juce::Rectangle<float> area) const
{
    paintShellPlaceholder(g, area, static_cast<float>(design::kDesignW), static_cast<float>(design::kDesignH));
}

void WebViewHost::resized()
{
    if (webView_ != nullptr)
        webView_->setBounds(revealGate_.parked() ? parkedBounds(getLocalBounds()) : getLocalBounds());
    if (fallback_ != nullptr)
        fallback_->setBounds(getLocalBounds());
}

void WebViewHost::applyRevealGate()
{
    if (webView_ == nullptr)
        return;
    resized();
    repaint(); // 挪走的那一刻要让占位底色立刻补上
}

void WebViewHost::noteRevealed()
{
    if (revealLogged_ || revealGate_.parked() || juce::String(revealGate_.lastRevealReason()).isEmpty())
        return;
    revealLogged_ = true;
    const juce::String reason(revealGate_.lastRevealReason());
    juce::String line = "webview revealed (" + reason + ") after " +
                        juce::String(static_cast<int>(juce::Time::getMillisecondCounter() - startMs_)) + " ms";
    if (reason == "timeout")
        line << " -- no first-frame signal before the deadline (navFinished "
             << (revealGate_.navigationFinishedSeen() ? "seen" : "not seen") << ")";
    logDiag(line);
}

void WebViewHost::resizeToDesignBox()
{
    const auto r = bridge::designBoxWindowSize(scale_.scale());
    setSize(r.width, r.height);
}

void WebViewHost::setScaleLocked(bool locked)
{
    scale_.setLocked(locked);
    if (fallback_ == nullptr)
        resizeToDesignBox();
    if (bridgeReady_)
        emitState();
}

juce::var WebViewHost::buildSnapshot()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("apiVersion", bridge::kApiVersion);
    obj->setProperty("lang", lang_);
    obj->setProperty("uiScale", bridge::scaleForJson(scale_.scale()));
    return juce::var(obj);
}

void WebViewHost::emitEvent(const char* eventId, const juce::var& payload)
{
    if (webView_ != nullptr)
        webView_->emitEventIfBrowserIsVisible(juce::Identifier(eventId), payload);
}

void WebViewHost::emitState()
{
    emitEvent(bridge::kStateEvent, buildSnapshot());
}

// 诊断行：给用户贴给维护者看，不含路径。basenameOnly 为真时用户数据目录只显示末级名字。
juce::String WebViewHost::buildDiagnostics(bool basenameOnly) const
{
    const char* nav = "notStarted";
    switch (navState_)
    {
    case NavState::started:
        nav = "started";
        break;
    case NavState::finished:
        nav = "finished";
        break;
    case NavState::networkError:
        nav = "networkError";
        break;
    case NavState::notStarted:
    default:
        break;
    }

    juce::String d;
    d << "waited " << juce::String(static_cast<int>(juce::Time::getMillisecondCounter() - startMs_)) << " ms"
      << "  |  nav " << nav;
    if (navDetail_.isNotEmpty())
        d << " (" << navDetail_ << ")";
    d << "  |  " << PlatformWebView::runtimeDiagnosticsField(runtime_);
    d << "  |  host " << juce::File::getSpecialLocation(juce::File::hostApplicationPath).getFileName() << " pid "
      << juce::String(PlatformWebView::processId());
    d << "\n"
      << "udf " << PlatformWebView::userDataFolderDisplay(userDataFolder_, basenameOnly);
    if (userDataFolderIssue_.isNotEmpty())
        d << "  [" << userDataFolderIssue_ << "]";
    if (bootError_.isNotEmpty())
        d << "\n" << bootError_;
    return d;
}

void WebViewHost::logDiag(const juce::String& line) const
{
    juce::Logger::writeToLog("Miciri: " + line);
}

void WebViewHost::showFallback(FallbackReason reason)
{
    if (fallback_ != nullptr)
        return;
    revealGate_.onFallbackShown();
    noteRevealed();
    webView_->setVisible(false);

    const bool missing = (reason == FallbackReason::MissingRuntime);
    const bool tooOld = (reason == FallbackReason::RuntimeTooOld);
    juce::String message;
    const char* tag = "loadTimeout";
    if (missing)
    {
        message = PlatformWebView::missingRuntimeMessage();
        tag = "missingRuntime";
    }
    else if (tooOld)
    {
        message = PlatformWebView::tooOldRuntimeMessage();
        tag = "runtimeTooOld";
    }
    else if (reason == FallbackReason::PageMissing)
    {
        message = pageMissingMessage();
        tag = "pageMissing";
    }
    else if (reason == FallbackReason::BootError)
    {
        message = bootErrorMessage();
        tag = "bootError";
    }
    else if (navState_ == NavState::notStarted)
    {
        message = PlatformWebView::envNotStartedMessage();
        tag = "envNotStarted";
    }
    else if (navState_ == NavState::finished)
    {
        message = bridgeStalledMessage();
        tag = "bridgeStalled";
    }
    else
    {
        message = navStalledMessage();
        tag = "navStalled";
    }
    logDiag(juce::String("fallback ") + tag + " - " + buildDiagnostics(false).replaceCharacter('\n', ' '));

    FallbackPanel::Options options;
    options.title = kFallbackTitle;
    options.message = message;
    options.details = buildDiagnostics(true);
    options.showInstall = (missing || tooOld) && PlatformWebView::offersRuntimeDownload();
    options.onInstall = [] { juce::URL(PlatformWebView::runtimeDownloadUrl()).launchInDefaultBrowser(); };
    // 点击回调里不能直接销毁面板自己：投递到消息队列，并用 SafePointer 防编辑器先被关掉。
    options.onRetry = [safeThis = juce::Component::SafePointer<WebViewHost>(this)] {
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->retryWebView();
        });
    };
    fallback_ = std::make_unique<FallbackPanel>(std::move(options));
    addAndMakeVisible(*fallback_);

    // 窗口比设计盒小（缩放档位低）时，兜底面板放不下文字，至少撑到设计盒大小。
    if (getWidth() < design::kDesignW || getHeight() < design::kDesignH)
        setSize(juce::jmax(getWidth(), design::kDesignW), juce::jmax(getHeight(), design::kDesignH));
    resized();
}

void WebViewHost::retryWebView()
{
    fallback_.reset();
    resizeToDesignBox();
    beginLoadAttempt(); // 重新探测运行时并重置看门狗；仍然缺失会就地再切兜底
    resized();
}

void WebViewHost::onNavigationStarted(const juce::String& url)
{
    navState_ = NavState::started;
    navDetail_ = {};
    if (!navBudgetApplied_)
    {
        navBudgetApplied_ = true; // 只顺延一次：页面反复导航不许无限推迟看门狗
        const auto extended = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(kAfterNavBudgetMs);
        if (static_cast<juce::int32>(extended - deadlineMs_) > 0) // 回绕安全的“更晚吗”
            deadlineMs_ = extended;
    }
    if (PlatformWebView::revealGateEnabled())
    {
        revealGate_.onNavigationStarted(juce::Time::getMillisecondCounter());
        applyRevealGate();
    }
    logDiag("navigation started: " + ResourceProvider::urlForLog(url, kNavUrlLogMaxChars));
}

void WebViewHost::onNavigationFinished(const juce::String& url)
{
    navState_ = NavState::finished;
    revealGate_.onNavigationFinished();
    logDiag("navigation finished: " + ResourceProvider::urlForLog(url, kNavUrlLogMaxChars));
}

void WebViewHost::onNavigationError(const juce::String& errorInfo)
{
    navState_ = NavState::networkError;
    navDetail_ = ResourceProvider::asciiForLog(errorInfo, kNavErrorMaxChars);
    logDiag("navigation error: " + navDetail_);
}

void WebViewHost::handleBootError(const juce::var& payload)
{
    bootError_ =
        "boot " +
        ResourceProvider::asciiForLog(payload.getProperty("stage", juce::var("unknown")).toString(),
                                      kBootStageMaxChars) +
        ": " +
        ResourceProvider::asciiForLog(payload.getProperty("detail", juce::var("")).toString(), kBootDetailMaxChars);
    logDiag("front-end " + bootError_);
    if (bridgeReady_ || fallback_ != nullptr)
        return; // 桥已经起来后的运行期错误归界面自己处理，不砸掉一个能用的界面
    showFallback(FallbackReason::BootError);
}

void WebViewHost::handleFirstFrame(const juce::var& payload)
{
    logDiag(juce::String("first-frame signal after ") +
            juce::String(static_cast<int>(juce::Time::getMillisecondCounter() - startMs_)) + " ms" +
            (revealGate_.parked()                   ? " (still parked)"
             : PlatformWebView::revealGateEnabled() ? " (already revealed)"
                                                    : " (reveal gate off)") +
            bridge::paintDeltaNote(payload, kPaintDeltaField));
    revealGate_.onFirstFrame(juce::Time::getMillisecondCounter());
    applyRevealGate();
    noteRevealed();
}

// 装配 WebView 选项：资源提供者、初始化数据、契约里的全部桥函数、两个诊断事件。
// 函数名取自 BridgeApi.h 的 kFunctions，统一进 dispatchBridgeCall。
juce::WebBrowserComponent::Options WebViewHost::makeOptions()
{
    auto options = PlatformWebView::makeWebViewOptions(WBC::Options{}, userDataFolder_);
    options = options.withNativeIntegrationEnabled().withResourceProvider(
        [this](const juce::String& url) { return provider_.provide(url); },
        juce::URL(WBC::getResourceProviderRoot()).getOrigin());

    options = options.withInitialisationData("apiVersion", bridge::kApiVersion)
                  .withInitialisationData("lang", lang_)
                  .withInitialisationData("uiScale", bridge::scaleForJson(scale_.scale()));

    for (const auto name : bridge::kFunctions)
    {
        const juce::String fnName(name.data(), name.size());
        options =
            options.withNativeFunction(juce::Identifier(fnName), [this, fnName](const juce::Array<juce::var>& args,
                                                                                WBC::NativeFunctionCompletion c) {
                dispatchBridgeCall(fnName, args, std::move(c));
            });
    }

    options = options.withEventListener(juce::Identifier(kBootErrorEventId),
                                        [this](const juce::var& payload) { handleBootError(payload); });
    options = options.withEventListener(juce::Identifier(kFirstFrameEventId),
                                        [this](const juce::var& payload) { handleFirstFrame(payload); });
    return options;
}

// 兜底面板显示时一律回 not-ready 且不改任何状态；否则交给（可被覆盖的）handleBridgeCall。
void WebViewHost::dispatchBridgeCall(const juce::String& name, const juce::Array<juce::var>& args,
                                     NativeCompletion complete)
{
    if (fallback_ != nullptr)
    {
        logDiag("bridge request ignored while the fallback panel is shown (" + name + ")");
        complete(bridge::respondNotReady());
        return;
    }
    handleBridgeCall(name, args, std::move(complete));
}

void WebViewHost::handleBridgeCall(const juce::String& name, const juce::Array<juce::var>& args,
                                   NativeCompletion complete)
{
    if (name == "requestInitialState")
    {
        bridgeReady_ = true; // 前端确认就绪，此后定时器才允许推送事件
        if (revealGate_.onBridgeReady(juce::Time::getMillisecondCounter()))
            logDiag("bridge ready while still held before navigation; reveal deadline starts now");
        if (!countedAsReady_)
        {
            countedAsReady_ = true;
            readyBridgeCount().fetch_add(1);
        }
        logDiag("bridge ready after " + juce::String(static_cast<int>(juce::Time::getMillisecondCounter() - startMs_)) +
                " ms");
        complete(buildSnapshot()); // 回执就是快照；页面首次装载和编辑器重建后都要调一次，桥不缓存
        return;
    }
    if (name == "setLang")
    {
        bool accepted = false;
        complete(bridge::respondSetLang(args, [this, &accepted](const juce::String& code) {
            lang_ = code;
            uidefaults::chooseLangGlobal(code); // 语言值和“用户显式选过”一次写完
            accepted = true;
        }));
        if (accepted)
            emitState();
        return;
    }
    if (name == "setUiScale")
    {
        bool windowChanged = false;
        auto response = bridge::respondSetUiScale(scale_, args, windowChanged);
        if (windowChanged)
            resizeToDesignBox(); // 只实时预览，不落盘
        complete(std::move(response));
        if (windowChanged)
            emitState();
        return;
    }
    if (name == "commitUiScale")
    {
        complete(bridge::respondCommitUiScale(
            scale_, [](float scale) { uidefaults::setUiScalePercent(bridge::scaleToPercent(scale)); }));
        return;
    }
    complete(bridge::respondNotReady());
}

void WebViewHost::timerCallback()
{
    if (revealGate_.parked())
    {
        if (revealGate_.holdingBeforeNavigation())
            webView_->pumpControllerCreation();
        revealGate_.onTick(juce::Time::getMillisecondCounter());
        if (!revealGate_.parked())
        {
            applyRevealGate();
            noteRevealed();
        }
    }
    if (!bridgeReady_ && fallback_ == nullptr &&
        static_cast<juce::int32>(juce::Time::getMillisecondCounter() - deadlineMs_) > 0)
    {
        showFallback(FallbackReason::LoadTimeout);
        return;
    }
    if (!bridgeReady_)
        return;
    emitTick();
}

void WebViewHost::emitTick() {}
} // namespace miciri::webview

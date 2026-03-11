#pragma once

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <webview/webview.h>
#include <cstring>
#include <string>
#include <stdexcept>

using namespace Microsoft::WRL;

/**
 * Sends frame data to JavaScript via the WebView2 Shared Buffer API.
 *
 * This avoids costly base64 encoding / JSON serialization by sharing memory
 * directly between the native process and the renderer process.
 *
 * Usage:
 *   SharedFrameSender sender(w);            // w = webview::webview&
 *   sender.send(frameData, dataSize, 1920, 1080, "RGBA");
 *
 * On the JS side, listen for:
 *   chrome.webview.addEventListener("sharedbufferreceived", handler);
 */
class SharedFrameSender {
public:
    explicit SharedFrameSender(webview::webview& w)
        : m_webview(w)
    {
        // Obtain ICoreWebView2Controller* from the webview library
        auto* controller = static_cast<ICoreWebView2Controller*>(
            m_webview.browser_controller().value());

        // Get ICoreWebView2*
        controller->get_CoreWebView2(&m_coreWebView);

        // QI to ICoreWebView2_2 to reach the environment
        ComPtr<ICoreWebView2_2> wv2;
        m_coreWebView->QueryInterface(IID_PPV_ARGS(&wv2));
        if (!wv2) {
            throw std::runtime_error("ICoreWebView2_2 not available");
        }

        ComPtr<ICoreWebView2Environment> env;
        wv2->get_Environment(&env);

        // QI to ICoreWebView2Environment12 (Shared Buffer support)
        env->QueryInterface(IID_PPV_ARGS(&m_env12));
        if (!m_env12) {
            throw std::runtime_error(
                "ICoreWebView2Environment12 not available – "
                "update the WebView2 Runtime");
        }

        // QI to ICoreWebView2_17 (PostSharedBufferToScript)
        m_coreWebView->QueryInterface(IID_PPV_ARGS(&m_wv17));
        if (!m_wv17) {
            throw std::runtime_error(
                "ICoreWebView2_17 not available – "
                "update the WebView2 Runtime");
        }
    }

    /**
     * Sends frame data to JavaScript via a shared buffer.
     * All-in-one convenience method (must be called from the main thread).
     *
     * @param frameData  Pointer to the raw pixel data
     * @param dataSize   Size in bytes
     * @param width      Frame width in pixels
     * @param height     Frame height in pixels
     * @param format     Pixel format string, e.g. "RGBA", "RGB", "BGRA"
     */
    void send(const uint8_t* frameData, size_t dataSize,
              uint32_t width, uint32_t height,
              const std::string& format = "RGBA")
    {
        ensureBuffer(dataSize);
        std::memcpy(bufferPtr(), frameData, dataSize);
        post(width, height, format);
    }

    /**
     * Ensures the shared buffer is at least @p dataSize bytes.
     * Must be called from the main thread (COM call).
     */
    void ensureBuffer(size_t dataSize)
    {
        if (!m_sharedBuffer || m_bufferSize != dataSize) {
            m_sharedBuffer.Reset();
            HRESULT hr = m_env12->CreateSharedBuffer(
                static_cast<UINT64>(dataSize), &m_sharedBuffer);
            if (FAILED(hr) || !m_sharedBuffer) {
                throw std::runtime_error("CreateSharedBuffer failed");
            }
            m_bufferSize = dataSize;
            m_sharedBuffer->get_Buffer(&m_bufferPtr);
        }
    }

    /**
     * Returns a raw pointer into the shared buffer memory.
     * Thread-safe once ensureBuffer() has been called.
     * The caller may write up to bufferSize() bytes.
     */
    BYTE* bufferPtr() const { return m_bufferPtr; }

    /**
     * Returns the current buffer size in bytes.
     */
    size_t bufferSize() const { return m_bufferSize; }

    /**
     * Posts the shared buffer to JavaScript.
     * Must be called from the main thread (COM call).
     * Call this after writing frame data into bufferPtr().
     */
    void post(uint32_t width, uint32_t height,
              const std::string& format = "RGBA", int frameNr = -1)
    {
        std::wstring meta = L"{\"width\":" + std::to_wstring(width)
            + L",\"height\":" + std::to_wstring(height)
            + L",\"format\":\"" + std::wstring(format.begin(), format.end()) + L"\""
            + L",\"byteLength\":" + std::to_wstring(m_bufferSize)
            + L",\"frameNr\":" + std::to_wstring(frameNr)
            + L"}";

        m_wv17->PostSharedBufferToScript(
            m_sharedBuffer.Get(),
            COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY,
            meta.c_str());
    }

private:
    webview::webview&                       m_webview;
    ICoreWebView2*                          m_coreWebView = nullptr;
    ComPtr<ICoreWebView2Environment12>      m_env12;
    ComPtr<ICoreWebView2_17>                m_wv17;
    ComPtr<ICoreWebView2SharedBuffer>       m_sharedBuffer;
    size_t                                  m_bufferSize = 0;
    BYTE*                                   m_bufferPtr = nullptr;
};

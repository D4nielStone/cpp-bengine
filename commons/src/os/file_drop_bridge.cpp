#include "file_drop_bridge.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <oleidl.h>
#include <shellapi.h>
#include <bgui.hpp>
#elif defined(BENGINE_X11_FILE_DRAG)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

namespace commons::detail {
#if defined(_WIN32)
namespace {
// OLE supplies enter/leave notifications that GLFW's WM_DROPFILES does not expose.
class drop_target final : public IDropTarget {
public:
    explicit drop_target(HWND window, file_drop_handler handler)
        : window(window), handler(std::move(handler)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_IDropTarget) return E_NOINTERFACE;
        *result = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        active = data && data->QueryGetData(&format) == S_OK;
        *effect = active && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        redraw();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = active && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        redraw();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        active = false;
        redraw();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        active = false;
        const bool copy = (*effect & DROPEFFECT_COPY) != 0;
        *effect = DROPEFFECT_NONE;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (copy && data && data->GetData(&format, &medium) == S_OK) {
            std::vector<std::filesystem::path> paths;
            auto drop = reinterpret_cast<HDROP>(medium.hGlobal);
            const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const auto length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                paths.emplace_back(path);
            }
            ReleaseStgMedium(&medium);
            POINT local{point.x, point.y};
            ScreenToClient(window, &local);
            handler(std::move(paths), local.x, local.y);
            *effect = DROPEFFECT_COPY;
        }
        redraw();
        return S_OK;
    }
    bool active{false};
private:
    static void redraw() {
        // OLE runs its own modal loop while dragging; keep the editor hint visible.
        const auto& refresh = bgui::get_context().m_refresh_func;
        if (refresh) refresh();
    }
    ULONG references{1};
    HWND window;
    file_drop_handler handler;
};
class windows_file_drop final : public file_drop_bridge {
public:
    windows_file_drop(GLFWwindow* window, file_drop_handler handler) : window(glfwGetWin32Window(window)) {
        const auto initialized = OleInitialize(nullptr);
        if (FAILED(initialized)) return; // GLFW still supplies drops via WM_DROPFILES.
        ole_initialized = true;
        target = new drop_target(this->window, std::move(handler));
        if (FAILED(RegisterDragDrop(this->window, target))) {
            target->Release();
            target = nullptr;
        } else DragAcceptFiles(this->window, FALSE);
    }
    ~windows_file_drop() override {
        if (target) {
            RevokeDragDrop(window);
            target->Release();
        }
        if (ole_initialized) OleUninitialize();
    }
    bool hovered() const override { return target && target->active; }
private:
    HWND window;
    drop_target* target{nullptr};
    bool ole_initialized{false};
};
}
#elif defined(BENGINE_X11_FILE_DRAG)
namespace {
class x11_file_drag final : public file_drop_bridge {
public:
    explicit x11_file_drag(GLFWwindow* window) : window(window) {}
    bool hovered() const override {
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        if (glfwGetPlatform() != GLFW_PLATFORM_X11) return false;
#endif
        auto* display = glfwGetX11Display();
        if (!display) return false;
        const auto selection = XInternAtom(display, "XdndSelection", True);
        if (!selection || XGetSelectionOwner(display, selection) == None) return false;
        ::Window root{}, child{};
        int root_x{}, root_y{}, x{}, y{}, width{}, height{};
        unsigned mask{};
        if (!XQueryPointer(display, glfwGetX11Window(window), &root, &child,
            &root_x, &root_y, &x, &y, &mask)) return false;
        glfwGetWindowSize(window, &width, &height);
        return x >= 0 && y >= 0 && x < width && y < height &&
            (mask & (Button1Mask | Button2Mask | Button3Mask));
    }
private:
    GLFWwindow* window;
};
}
#endif

std::unique_ptr<file_drop_bridge> make_file_drop_bridge(GLFWwindow* window, file_drop_handler handler) {
#if defined(_WIN32)
    return std::make_unique<windows_file_drop>(window, std::move(handler));
#elif defined(BENGINE_X11_FILE_DRAG)
    return std::make_unique<x11_file_drag>(window);
#else
    return std::make_unique<file_drop_bridge>();
#endif
}
}

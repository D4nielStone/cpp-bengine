#include "file_drop_bridge.hpp"
#include "file_drag_session.hpp"
#include <array>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <glad.h>
#include <bgui.hpp>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <oleidl.h>
#include <shellapi.h>
#elif defined(BENGINE_X11_FILE_DRAG)
#include <X11/Xatom.h>
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
    explicit x11_file_drag(GLFWwindow* window) : window(window) {
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        if (glfwGetPlatform() != GLFW_PLATFORM_X11) return;
#endif
        display = glfwGetX11Display();
        if (!display) return;
        native = glfwGetX11Window(window);
        enter = XInternAtom(display, "XdndEnter", False);
        leave = XInternAtom(display, "XdndLeave", False);
        drop = XInternAtom(display, "XdndDrop", False);
        selection = XInternAtom(display, "XdndSelection", False);
        type_list = XInternAtom(display, "XdndTypeList", False);
        uri_list = XInternAtom(display, "text/uri-list", False);
    }
    void poll_events() override {
        if (!display) return;
        struct pending_messages {
            x11_file_drag* bridge;
            std::array<XEvent, 64> events{};
            std::size_t count{0};
            bool overflow{false};
        } pending{this};
        XEvent ignored{};
        // Observe without stealing GLFW events. The predicate only copies data:
        // property queries must happen after Xlib releases the queue's lock.
        XCheckIfEvent(display, &ignored, [](Display*, XEvent* event, XPointer self) -> Bool {
            auto& pending = *reinterpret_cast<pending_messages*>(self);
            const auto* bridge = pending.bridge;
            if (event->type == ClientMessage && event->xclient.window == bridge->native &&
                (event->xclient.message_type == bridge->enter || event->xclient.message_type == bridge->leave ||
                    event->xclient.message_type == bridge->drop)) {
                if (pending.count < pending.events.size()) pending.events[pending.count++] = *event;
                else pending.overflow = true;
            }
            return False;
        }, reinterpret_cast<XPointer>(&pending));
        if (pending.overflow) session.enter(0, false);
        else for (std::size_t i = 0; i < pending.count; ++i) observe(pending.events[i]);
        (void)hovered(); // Clear ended sessions even while a modal hides the hint.
    }
    bool hovered() const override {
        if (!display) return false;
        ::Window root{}, child{};
        int root_x{}, root_y{}, x{}, y{}, width{}, height{};
        unsigned mask{};
        if (!XQueryPointer(display, native, &root, &child,
            &root_x, &root_y, &x, &y, &mask)) {
            session.enter(0, false);
            return false;
        }
        glfwGetWindowSize(window, &width, &height);
        const bool inside = glfwGetWindowAttrib(window, GLFW_VISIBLE) &&
            !glfwGetWindowAttrib(window, GLFW_ICONIFIED) &&
            x >= 0 && y >= 0 && x < width && y < height;
        return session.hovered(XGetSelectionOwner(display, selection), inside,
            (mask & (Button1Mask | Button2Mask | Button3Mask)) != 0);
    }
private:
    void observe(const XEvent& event) {
        if (event.type != ClientMessage || event.xclient.window != native || event.xclient.format != 32) return;
        const auto source = static_cast<::Window>(event.xclient.data.l[0]);
        if (event.xclient.message_type == leave || event.xclient.message_type == drop) {
            session.leave(source);
        } else if (event.xclient.message_type == enter) {
            bool files = false;
            if (event.xclient.data.l[1] & 1) {
                Atom actual_type{};
                int format{};
                unsigned long count{}, remaining{};
                unsigned char* data = nullptr;
                if (XGetWindowProperty(display, source, type_list, 0, 256, False,
                    XA_ATOM, &actual_type, &format, &count, &remaining, &data) == Success &&
                    actual_type == XA_ATOM && format == 32 && data) {
                    const auto* types = reinterpret_cast<Atom*>(data);
                    for (unsigned long i = 0; i < count; ++i) files |= types[i] == uri_list;
                }
                if (data) XFree(data);
            } else {
                for (int i = 2; i < 5; ++i)
                    files |= static_cast<Atom>(event.xclient.data.l[i]) == uri_list;
            }
            session.enter(source, files);
        }
    }
    GLFWwindow* window;
    Display* display{nullptr};
    ::Window native{};
    Atom enter{}, leave{}, drop{}, selection{}, type_list{}, uri_list{};
    mutable file_drag_session session;
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

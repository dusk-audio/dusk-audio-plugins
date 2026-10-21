// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <filesystem>
#include <string>

// A separate native drop target: it negotiates the advertised URI type and
// reads the actual X selection instead of calling any plugin export function.
struct FileDropTarget
{
    Display* display;
    Window window, source = 0;
    Atom enter, position, status, leave, drop, finished, selection, uri, property, copy;
    unsigned enters = 0, positions = 0, drops = 0, leaves = 0, transfers = 0;
    bool offersUri = false, copyOnly = true, accepts, preview, requestedPreview = false;
    std::string data;

    explicit FileDropTarget(Display* d, bool accept = true, bool inspect = false)
        : display(d), accepts(accept), preview(inspect)
    {
        const auto atom = [&](const char* name) {return XInternAtom(display, name, False);};
        enter = atom("XdndEnter"); position = atom("XdndPosition"); status = atom("XdndStatus");
        leave = atom("XdndLeave"); drop = atom("XdndDrop"); finished = atom("XdndFinished");
        selection = atom("XdndSelection"); uri = atom("text/uri-list");
        property = atom("DuskDragPayload"); copy = atom("XdndActionCopy");
        XSetWindowAttributes attributes{}; attributes.override_redirect = True;
        window = XCreateWindow(display, DefaultRootWindow(display), 1000, 50, 240, 400, 0,
            CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
        const unsigned long version = 5;
        XChangeProperty(display, window, atom("XdndAware"), XA_ATOM, 32, PropModeReplace,
            reinterpret_cast<const unsigned char*>(&version), 1);
        XMapRaised(display, window); XSync(display, False);
    }
    FileDropTarget(const FileDropTarget&) = delete;
    FileDropTarget& operator=(const FileDropTarget&) = delete;
    ~FileDropTarget() {XDestroyWindow(display, window); XSync(display, False);}

    void send(Atom type, long flags, long action)
    {
        XEvent event{};
        event.xclient.type = ClientMessage; event.xclient.display = display;
        event.xclient.window = source; event.xclient.message_type = type; event.xclient.format = 32;
        event.xclient.data.l[0] = window; event.xclient.data.l[1] = flags;
        event.xclient.data.l[type == status ? 4 : 2] = action;
        XSendEvent(display, source, False, NoEventMask, &event); XFlush(display);
    }
    void pump()
    {
        while (XPending(display))
        {
            XEvent event{}; XNextEvent(display, &event);
            if (event.type == ClientMessage && event.xclient.window == window)
            {
                if (event.xclient.message_type == enter)
                {
                    source = event.xclient.data.l[0]; ++enters; offersUri = false;
                    if (event.xclient.data.l[1] & 1)
                    {
                        Atom type = None; int format = 0; unsigned long count = 0, left = 0; unsigned char* bytes = nullptr;
                        XGetWindowProperty(display, source, XInternAtom(display, "XdndTypeList", False), 0, 1024, False,
                            XA_ATOM, &type, &format, &count, &left, &bytes);
                        if (type == XA_ATOM && format == 32 && left == 0 && bytes)
                            for (unsigned long i = 0; i < count; ++i)
                                if (reinterpret_cast<Atom*>(bytes)[i] == uri) offersUri = true;
                        if (bytes) XFree(bytes);
                    }
                    else for (int i = 2; i < 5; ++i)
                        if (Atom(event.xclient.data.l[i]) == uri) offersUri = true;
                }
                if (event.xclient.message_type == position)
                {
                    ++positions;
                    copyOnly = copyOnly && Atom(event.xclient.data.l[4]) == copy;
                    send(status, offersUri && accepts ? 3 : 0, offersUri && accepts ? copy : None);
                    if (offersUri && preview && !requestedPreview)
                    {
                        requestedPreview = true;
                        XConvertSelection(display, selection, uri, property, window, event.xclient.data.l[3]);
                        XFlush(display);
                    }
                }
                if (event.xclient.message_type == leave) ++leaves;
                if (event.xclient.message_type == drop)
                {
                    ++drops;
                    XConvertSelection(display, selection, uri, property, window, event.xclient.data.l[2]);
                    XFlush(display);
                }
            }
            if (event.type == SelectionNotify && event.xselection.requestor == window && event.xselection.property == property)
            {
                Atom type = None; int format = 0; unsigned long count = 0, left = 0; unsigned char* bytes = nullptr;
                if (XGetWindowProperty(display, window, property, 0, 65536, True, uri, &type, &format, &count, &left, &bytes) == Success
                    && type == uri && format == 8 && left == 0 && bytes)
                {data.assign(reinterpret_cast<char*>(bytes), count); ++transfers;}
                if (bytes) XFree(bytes);
                if (drops) send(finished, 1, copy);
            }
        }
    }
    std::filesystem::path file() const
    {
        if (data.compare(0, 8, "file:///") || data.size() < 11 || data.substr(data.size() - 2) != "\r\n") return {};
        const auto digit = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        std::string path;
        for (std::size_t i = 7; i + 2 < data.size(); ++i)
        {
            unsigned char byte = static_cast<unsigned char>(data[i]);
            const bool encoded = byte == '%';
            if (byte == '%')
            {
                if (i + 4 >= data.size()) return {};
                const int high = digit(data[i + 1]), low = digit(data[i + 2]);
                if (high < 0 || low < 0) return {};
                byte = static_cast<unsigned char>(high * 16 + low); i += 2;
            }
            if (byte == 0 || (!encoded && (byte == '\r' || byte == '\n'))) return {};
            path += static_cast<char>(byte);
        }
        return std::filesystem::u8path(path);
    }
};

#include "native_file_dialog.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#endif

namespace editor {
std::filesystem::path browse_native(const std::filesystem::path& initial,
                                    bool directory, std::string& error) {
    error.clear();
#ifdef _WIN32
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        error = "Could not initialize the system file dialog.";
        return {};
    }
    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    std::filesystem::path selection;
    if (SUCCEEDED(result)) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
            (directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
        const COMDLG_FILTERSPEC filter[] = {
            {L"Bubble Engine projects and scenes", L"*.bengine;*.scene;*.world"}};
        if (!directory)
            dialog->SetFileTypes(1, filter);
        std::error_code ec;
        auto folder = std::filesystem::is_directory(initial, ec)
            ? initial : initial.parent_path();
        IShellItem* start = nullptr;
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(
                folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dialog->SetFolder(start);
            start->Release();
        }
        result = dialog->Show(GetActiveWindow());
        if (SUCCEEDED(result)) {
            IShellItem* item = nullptr;
            result = dialog->GetResult(&item);
            if (SUCCEEDED(result)) {
                PWSTR path = nullptr;
                result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result)) {
                    selection = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        error = "Could not open the system file dialog.";
    return selection;
#else
    // Pass paths as individual arguments, never through a shell.
    std::error_code initial_error;
    auto initial_name = initial.string();
    if (std::filesystem::is_directory(initial, initial_error) &&
        !initial_name.empty() && initial_name.back() != '/')
        initial_name += '/';
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    const bool kde = desktop && std::strstr(desktop, "KDE");
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool use_kdialog = attempt == 0 ? kde : !kde;
        std::vector<std::string> args;
        if (use_kdialog) {
            args = {"kdialog", directory ? "--getexistingdirectory" : "--getopenfilename",
                    initial.string()};
            if (!directory)
                args.emplace_back("*.bengine *.scene *.world|Bubble Engine projects and scenes");
        } else {
            args = {"zenity", "--file-selection", "--title=Bubble Engine",
                    "--filename=" + initial_name};
            if (directory)
                args.emplace_back("--directory");
            else
                args.emplace_back("--file-filter=Bubble Engine | *.bengine *.scene *.world");
        }
        std::vector<char*> argv;
        for (auto& arg : args)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        int descriptors[2];
        if (pipe(descriptors) != 0) {
            error = "Could not create the system file dialog pipe.";
            return {};
        }
        const pid_t child = fork();
        if (child == 0) {
            close(descriptors[0]);
            if (dup2(descriptors[1], STDOUT_FILENO) == -1)
                _exit(126);
            close(descriptors[1]);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(descriptors[1]);
        if (child < 0) {
            close(descriptors[0]);
            error = "Could not start the system file dialog.";
            return {};
        }
        std::string output;
        char buffer[4096];
        ssize_t count;
        while ((count = read(descriptors[0], buffer, sizeof(buffer))) != 0) {
            if (count > 0)
                output.append(buffer, static_cast<std::size_t>(count));
            else if (errno != EINTR)
                break;
        }
        close(descriptors[0]);
        int status = 0;
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited >= 0 && WIFEXITED(status)) {
            const int code = WEXITSTATUS(status);
            if (code == 127)
                continue;
            if (code == 1)
                return {};
            if (code == 0) {
                if (!output.empty() && output.back() == '\n')
                    output.pop_back();
                return std::filesystem::path(output);
            }
        }
        error = "The system file dialog failed.";
        return {};
    }
    error = "Install Zenity or KDialog to browse files and folders.";
    return {};
#endif
}
}

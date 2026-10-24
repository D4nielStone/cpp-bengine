#include "project_preview.hpp"

#include <glad.h>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#else
#include <png.h>
#endif

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace editor::ui {
namespace {

std::vector<unsigned char> read_camera_frame(
    const commons::camera& camera,
    const int width,
    const int height
) {
    const auto byte_count = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 3;
    std::vector<unsigned char> pixels(byte_count);

    GLint previous_texture = 0;
    GLint previous_pack_alignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, camera.framebuffer_texture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);

    const auto row_size = static_cast<std::size_t>(width) * 3;
    std::vector<unsigned char> row(row_size);
    for (int y = 0; y < height / 2; ++y) {
        auto* top = pixels.data() + static_cast<std::size_t>(y) * row_size;
        auto* bottom = pixels.data() +
            static_cast<std::size_t>(height - y - 1) * row_size;
        std::copy_n(top, row_size, row.data());
        std::copy_n(bottom, row_size, top);
        std::copy_n(row.data(), row_size, bottom);
    }
    return pixels;
}

bool write_png(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::vector<unsigned char>& pixels,
    std::string& error
) {
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool initialized = SUCCEEDED(init_result);
    if (FAILED(init_result) && init_result != RPC_E_CHANGED_MODE) {
        error = "Could not initialize Windows Imaging Component.";
        return false;
    }
    struct com_apartment {
        bool initialized;
        ~com_apartment() {
            if (initialized)
                CoUninitialize();
        }
    } apartment{initialized};

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    ComPtr<IWICStream> stream;
    if (SUCCEEDED(result))
        result = factory->CreateStream(&stream);
    if (SUCCEEDED(result))
        result = stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result))
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result))
        result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (SUCCEEDED(result))
        result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result))
        result = frame->Initialize(properties.Get());
    if (SUCCEEDED(result))
        result = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat24bppRGB;
    if (SUCCEEDED(result))
        result = frame->SetPixelFormat(&pixel_format);
    if (SUCCEEDED(result) && pixel_format != GUID_WICPixelFormat24bppRGB)
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(result))
        result = frame->WritePixels(
            static_cast<UINT>(height),
            static_cast<UINT>(width * 3),
            static_cast<UINT>(pixels.size()),
            const_cast<BYTE*>(pixels.data())
        );
    if (SUCCEEDED(result))
        result = frame->Commit();
    if (SUCCEEDED(result))
        result = encoder->Commit();
    if (FAILED(result)) {
        error = "Windows Imaging Component could not encode the project preview.";
        return false;
    }
    return true;
#else
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(width);
    image.height = static_cast<png_uint_32>(height);
    image.format = PNG_FORMAT_RGB;
    if (!png_image_write_to_file(
            &image,
            path.string().c_str(),
            0,
            pixels.data(),
            0,
            nullptr
        )) {
        error = image.message;
        return false;
    }
    return true;
#endif
}

}

std::filesystem::path project_preview_path(const std::string& project_path) {
    return std::filesystem::path(project_path + ".preview.png");
}

bool save_project_preview(
    const std::string& project_path,
    const std::shared_ptr<commons::camera>& camera,
    std::string& error
) {
    error.clear();
    if (!camera || !camera->flag_fb || camera->framebuffer_texture() == 0) {
        error = "The scene camera framebuffer is unavailable.";
        return false;
    }

    const int width = camera->viewportFBO.x;
    const int height = camera->viewportFBO.y;
    if (width <= 0 || height <= 0) {
        error = "The scene camera framebuffer has no image size.";
        return false;
    }

    const auto target = project_preview_path(project_path);
    std::error_code filesystem_error;
    if (!target.parent_path().empty())
        std::filesystem::create_directories(target.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the project preview folder: " +
            filesystem_error.message();
        return false;
    }

    return write_png(target, width, height, read_camera_frame(*camera, width, height), error);
}

}

#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include "commons_namespace.hpp"
#include "utils/vec.hpp"
#include "util/color.hpp"

namespace COMMONS_NS {
    class shader_exception : public std::exception
    {
    public:
        shader_exception(const char* msg);
        virtual const char* what() const noexcept override;
    private:
        std::string msg_;
    };

    void desload_shaders();

    class shader
    {
    public:
        std::string vert, frag;
        // the program ID
        unsigned int ID;

        // constructor reads and builds the shader
        shader() { compilar("phong.vert", "phong.frag"); };
        shader(const char* vertexPath, const char* fragmentPath);
        // use/activate the shader
        void use();
        // utility uniform functions
        void compilar(const char* vertexPath, const char* fragmentPath);
        void setBool (const std::string& name, const bool& value) const;
        void setInt  (const std::string& name, const int& value) const;
        void setFloat(const std::string& name, const float& value) const;
        void set_color  (const std::string& name, const color& m_color) const;
        void setVec4 (const std::string& name, const bgui::vec4& vec4) const;
        void setVec3 (const std::string& name, const float& r, const float& g, const float& b) const;
        void setVec3 (const std::string &name, const bgui::vec3 &vector_type) const;
        void setVec2 (const std::string &name, const float &r, const float &g) const;
        void setMat4 (const std::string& name, const float* value) const;
        void setMat3 (const std::string& name, const float* value) const;
        bool checkCompileErrors(unsigned int shader, const std::string& type);
        bool checkLinkErrors(unsigned int shader);
    private:
        int uniform_location(const std::string& name) const;
    };
    inline std::unordered_map<std::string, unsigned int> shaders;
}

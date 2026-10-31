#include "runtime_template.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        const std::string action = argc > 1 ? argv[1] : "";
        if (action == "prepare" && argc == 6)
            game_package::prepare(argv[2], argv[3], argv[4], argv[5]);
        else if (action == "stage" && argc == 5)
            game_package::stage_binary(argv[2], argv[3], std::string(argv[4]) == "windows");
        else if (action == "publish" && (argc == 4 || argc == 5))
            game_package::publish(argv[2], argv[3], argc == 5 && std::string(argv[4]) == "single");
        else if (action == "template-key" && argc == 5)
            std::cout << game_package::template_key(argv[2],argv[3],argv[4]) << '\n';
        else if (action == "template-check" && argc == 6) {
            try { game_package::validate_template(argv[2],argv[3],argv[4],argv[5]); return 0; }
            catch (const std::exception&) { return 1; }
        }
        else if (action == "template-validate" && argc == 6)
            game_package::validate_template(argv[2],argv[3],argv[4],argv[5]);
        else if (action == "template-create" && argc == 7)
            game_package::create_template(argv[2],argv[3],argv[4],argv[5],argv[6]);
        else throw std::runtime_error("Usage: game_packager prepare <repo> <project> <staging> <output> | stage <binary> <staging> <target> | publish <staging> <output>");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game export failed: " << error.what() << '\n';
        return 1;
    }
}

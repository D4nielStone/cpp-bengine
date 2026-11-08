#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace editor::ui::visual_lua {
enum class kind { raw, command, assignment, function, condition, branch, loop, while_loop, repeat, scope, scene_load };
struct block {
    std::uint64_t id{};
    kind type{kind::raw};
    std::string head, tail;
    std::vector<block> children;
};
struct token { std::string text; std::size_t begin{}, end{}; bool literal{}, comment{}; };
std::vector<token> lex(const std::string& source);
std::vector<block> parse(const std::string& source);
std::string emit(const std::vector<block>& blocks);
bool validate(const std::string& source, std::string& error);
const char* label(kind type);
block make(kind type, std::uint64_t id, const std::string& newline = "\n");
struct transition { std::string target; bool dynamic{}, reload{}; };
std::vector<transition> transitions(const std::string& source);
}

namespace editor::ui::visual_lua {
struct slot { std::string label; std::size_t begin{}, end{}; bool tail{}; };
struct command { std::string category, title, source; std::vector<std::string> arguments; };
const std::vector<command>& commands();
const command* describe(const block& value);
std::string title(const block& value);
std::vector<slot> slots(const block& value);
class document {
public:
    void load(const std::string& source);
    const std::vector<block>& blocks() const { return m_blocks; }
    std::string source() const { return emit(m_blocks); }
    const block* find(std::uint64_t id) const;
    bool replace(std::uint64_t id, const std::string& head, const std::string& tail);
    bool edit(std::uint64_t id, std::size_t slot_index, const std::string& value);
    bool insert(std::uint64_t parent, kind type, std::uint64_t before = 0);
    bool insert_command(std::uint64_t parent, std::size_t command_index, std::uint64_t before = 0);
    std::string copy(std::uint64_t id) const;
    bool paste(std::uint64_t parent, const std::string& source, std::uint64_t before = 0, std::string* error = nullptr);
    bool erase(std::uint64_t id);
    bool move(std::uint64_t id, std::uint64_t parent, std::uint64_t before = 0);
    bool undo();
    bool redo();
private:
    std::vector<block> m_blocks;
    std::vector<std::string> m_undo, m_redo;
    void remember(const std::string& previous);
};
}

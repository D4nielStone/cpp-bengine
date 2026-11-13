#include "visual_lua.hpp"
#include <lua.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <iterator>

namespace editor::ui::visual_lua {
namespace {
bool word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
std::size_t long_end(const std::string& s, std::size_t at) {
    if (at >= s.size() || s[at] != '[') return at;
    auto p = at + 1;
    while (p < s.size() && s[p] == '=') ++p;
    if (p == s.size() || s[p] != '[') return at;
    const auto close = "]" + s.substr(at + 1, p - at - 1) + "]";
    const auto end = s.find(close, p + 1);
    return end == std::string::npos ? s.size() : end + close.size();
}
// Partition source without rewriting bytes. Control headers can span lines;
// compact bodies are split at lexical keywords, never inside strings/comments.
std::vector<std::string> units(const std::string& source) {
    const auto tokens = lex(source);
    std::vector<std::string> result;
    std::size_t start = 0;
    int brackets = 0;
    bool header = false;
    const auto split = [&](std::size_t end) {
        if (end > start) { result.push_back(source.substr(start, end-start)); start=end; }
    };
    std::size_t previous = 0;
    for (std::size_t i=0; i<tokens.size(); ++i) {
        const auto& t=tokens[i];
        if (!brackets && !header) {
            const auto newline=source.find('\n',previous);
            if(newline<t.begin) split(newline+1);
        }
        if (!t.literal && !brackets) {
            if(t.text=="end" || t.text=="else" || t.text=="elseif" || t.text=="until") split(t.begin);
            if(t.text=="if" || t.text=="elseif" || t.text=="for" || t.text=="while") header=true;
            if(t.text=="then" || t.text=="do" || t.text=="else" || t.text=="end") {
                header=false;
                // Keep separating whitespace with the header/closing keyword.
                auto end=t.end;
                while(end<source.size() && std::isspace(static_cast<unsigned char>(source[end]))) {
                    if(source[end++]=='\n') break;
                }
                split(end);
            }
        }
        if(!t.literal) {
            if(t.text=="(" || t.text=="{" || t.text=="[") ++brackets;
            if(t.text==")" || t.text=="}" || t.text=="]") --brackets;
        }
        previous=t.end;
    }
    split(source.size());
    return result;
}
std::vector<token> code_tokens(const std::string& text) {
    auto tokens=lex(text);
    std::erase_if(tokens,[](const token& t){return t.comment;});
    return tokens;
}
kind classify(const std::vector<token>& t) {
    if (t.empty()) return kind::raw;
    const auto& first = t.front().text;
    const auto& last = t.back().text;
    if ((first == "function" || (first == "local" && t.size() > 1 && t[1].text == "function")) && last == ")") return kind::function;
    if (first == "if" && last == "then") return kind::condition;
    if ((first == "elseif" && last == "then") || (first == "else" && t.size() == 1)) return kind::branch;
    if (first == "for" && last == "do") return kind::loop;
    if (first == "while" && last == "do") return kind::while_loop;
    if (first == "repeat" && t.size() == 1) return kind::repeat;
    if (first == "do" && t.size() == 1) return kind::scope;
    // Avoid exposing fragments of unsupported anonymous functions.
    for (const auto& v : t)
        if (!v.literal && (v.text == "function" || v.text == "end" || v.text == "then" || v.text == "do" || v.text == "until")) return kind::raw;
    if (first == "local" || std::any_of(t.begin(), t.end(), [](const token& v) { return v.text == "="; })) return kind::assignment;
    return kind::command;
}
struct parser {
    std::vector<std::string> lines;
    std::size_t at{};
    std::uint64_t next{1};
    bool failed{};
    std::vector<block> body(bool nested, unsigned depth=0) {
        if(depth>128) {failed=true;return {};}
        std::vector<block> out;
        while (at < lines.size()) {
            auto tokens = code_tokens(lines[at]);
            if (!tokens.empty() && (tokens[0].text == "end" || tokens[0].text == "until" || tokens[0].text == "else" || tokens[0].text == "elseif")) {
                if (nested) return out;
                failed = true;
            }
            block value{next++, classify(tokens), lines[at++], {}, {}};
            const bool container = value.type == kind::function || value.type == kind::condition || value.type == kind::loop || value.type == kind::while_loop || value.type == kind::scope || value.type == kind::repeat;
            if (container) {
                value.children = body(true,depth+1);
                if (value.type == kind::condition) {
                    while (at < lines.size()) {
                        auto branch_tokens = code_tokens(lines[at]);
                        if (branch_tokens.empty() || (branch_tokens[0].text != "else" && branch_tokens[0].text != "elseif")) break;
                        block branch{next++, kind::branch, lines[at++], {}, {}};
                        branch.children = body(true,depth+1);
                        value.children.push_back(std::move(branch));
                    }
                }
                if (at == lines.size()) { failed = true; return out; }
                auto closing = code_tokens(lines[at]);
                if (closing.empty() || (value.type == kind::repeat ? closing[0].text != "until" : closing[0].text != "end")) { failed = true; return out; }
                // A closing line with subsequent statements cannot be rewritten independently.
                if (value.type != kind::repeat && closing.size() != 1) failed = true;
                value.tail = lines[at++];
            }
            if(value.type==kind::raw && !out.empty() && out.back().type==kind::raw) out.back().head+=value.head;
            else out.push_back(std::move(value));
        }
        return out;
    }
};
}
std::vector<token> lex(const std::string& source) {
    std::vector<token> tokens;
    for (std::size_t p = 0; p < source.size();) {
        if (std::isspace(static_cast<unsigned char>(source[p]))) { ++p; continue; }
        if (source.compare(p, 2, "--") == 0) {
            const auto begin=p;
            const auto end = long_end(source, p + 2);
            if (end != p + 2) p = end;
            else { p = source.find('\n', p); if (p == std::string::npos) p=source.size(); }
            tokens.push_back({source.substr(begin,p-begin),begin,p,true,true});
            continue;
        }
        const auto begin = p;
        bool literal = false;
        const auto end = long_end(source, p);
        if (end != p) { p = end; literal = true; }
        else if (source[p] == '\'' || source[p] == '"') {
            const auto quote = source[p++]; literal = true;
            while (p < source.size()) {
                if (source[p] == '\\') { p = std::min(p + 2, source.size()); continue; }
                if (source[p++] == quote) break;
            }
        } else if (word(source[p])) { while (p < source.size() && word(source[p])) ++p; }
        else {
            ++p;
            if (p < source.size() && (source.substr(begin,2) == "==" || source.substr(begin,2) == "~=" || source.substr(begin,2) == "<=" || source.substr(begin,2) == ">=" || source.substr(begin,2) == ".." || source.substr(begin,2) == "::")) ++p;
        }
        tokens.push_back({source.substr(begin,p-begin),begin,p,literal});
    }
    return tokens;
}
std::vector<block> parse(const std::string& source) {
    if (source.empty()) return {};
    std::string error;
    if(!validate(source,error)) return {{1,kind::raw,source,{},{}}};
    parser p{units(source)};
    auto result = p.body(false);
    if (p.failed || emit(result) != source) return {{1,kind::raw,source,{},{}}};
    return result;
}
std::string emit(const std::vector<block>& blocks) {
    std::string source;
    for (const auto& value : blocks) source += value.head + emit(value.children) + value.tail;
    return source;
}
bool validate(const std::string& source, std::string& error) {
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    if (!state) { error = "Não foi possível criar o verificador Lua."; return false; }
    const int status = luaL_loadbufferx(state.get(), source.data(), source.size(), "visual", "t");
    if (status != LUA_OK) { error = lua_tostring(state.get(), -1); return false; }
    error.clear(); return true;
}
const char* label(kind type) {
    switch(type) {
    case kind::function: return "Função / evento";
    case kind::condition: return "Se";
    case kind::branch: return "Senão";
    case kind::loop: return "Repetir";
    case kind::while_loop: return "Enquanto";
    case kind::scene_load: return "Trocar cena";
    case kind::repeat: return "Repetir até";
    case kind::scope: return "Escopo";
    case kind::assignment: return "Variável / atribuição";
    case kind::command: return "Comando";
    default: return "Lua preservado";
    }
}
block make(kind type, std::uint64_t id, const std::string& nl) {
    block b{id,type,{},{},{}};
    switch(type) {
    case kind::function: b.head="function update()"; b.tail="end"+nl; break;
    case kind::condition: b.head="if true then"; b.tail="end"+nl; break;
    case kind::while_loop: b.head="while true do"; b.tail="end"+nl; break;
    case kind::scene_load: b.head="SceneManager.LoadScene(\"assets/next.scene\")"; break;
    case kind::loop: b.head="for i = 1, 10 do"; b.tail="end"+nl; break;
    case kind::repeat: b.head="repeat"; b.tail="until true"+nl; break;
    case kind::scope: b.head="do"; b.tail="end"+nl; break;
    case kind::assignment: b.head="local value = 0"; break;
    case kind::branch: b.head="else"; break;
    default: b.head="Debug.Log(\"Olá\")"; break;
    }
    b.head+=nl; return b;
}
std::vector<transition> transitions(const std::string& source) {
    const auto tokens = code_tokens(source);
    std::vector<transition> result;
    for (std::size_t i=0; i+3<tokens.size(); ++i) {
        if (tokens[i].literal || tokens[i].text!="SceneManager" || tokens[i+1].text!="." || tokens[i+3].text!="(") continue;
        if (i && (tokens[i-1].text=="function" || tokens[i-1].text=="." || tokens[i-1].text==":")) continue;
        if (tokens[i+2].text=="ReloadScene") { result.push_back({{},false,true}); continue; }
        if (tokens[i+2].text!="LoadScene") continue;
        if (i+5<tokens.size() && tokens[i+4].literal && tokens[i+5].text==")") {
            // Decode using Lua's own string grammar without running user code.
            const auto expression="return "+tokens[i+4].text;
            std::unique_ptr<lua_State,decltype(&lua_close)> state(luaL_newstate(),&lua_close);
            if (state && luaL_loadbufferx(state.get(),expression.data(),expression.size(),"literal","t")==LUA_OK && lua_pcall(state.get(),0,1,0)==LUA_OK) {
                std::size_t size{}; const char* text=lua_tolstring(state.get(),-1,&size);
                if(text) { result.push_back({std::string(text,size),false,false}); continue; }
            }
        }
        result.push_back({{},true,false});
    }
    return result;
}
}

namespace editor::ui::visual_lua {
namespace {
block* find_block(std::vector<block>& values,std::uint64_t id) {
    for(auto& b:values) { if(b.id==id) return &b; if(auto* found=find_block(b.children,id)) return found; }
    return nullptr;
}
const block* find_block(const std::vector<block>& values,std::uint64_t id) {
    for(const auto& b:values) { if(b.id==id) return &b; if(const auto* found=find_block(b.children,id)) return found; }
    return nullptr;
}
std::vector<block>* parent_list(std::vector<block>& values,std::uint64_t id) {
    for(auto& b:values) { if(b.id==id) return &values; if(auto* found=parent_list(b.children,id)) return found; }
    return nullptr;
}
bool container(kind k) { return k==kind::function || k==kind::condition || k==kind::loop || k==kind::while_loop || k==kind::repeat || k==kind::scope || k==kind::branch; }
void terminate(std::vector<block>& values,const std::string& nl) {
    if(values.empty()) return;
    auto& last=values.back();
    auto& text=last.tail.empty()?last.head:last.tail;
    if(!text.empty() && text.back()!='\n') text+=nl;
}
}
namespace {
// Lexical ranges keep comments, nested expressions and original formatting intact.
std::vector<slot> arguments(const std::vector<token>& t, std::size_t first, std::size_t last, unsigned level=0) {
    std::vector<slot> result;
    if(first>=last || level>32) return result;
    std::size_t start=first;
    int depth=0, functions=0;
    for(std::size_t i=first;i<=last;++i) {
        if(i<last && !t[i].literal) {
            if(t[i].text=="function") ++functions;
            if(t[i].text=="end" && functions) --functions;
            if(t[i].text=="(" || t[i].text=="[" || t[i].text=="{") ++depth;
            if(t[i].text==")" || t[i].text=="]" || t[i].text=="}") --depth;
        }
        if(i==last || (!depth && !functions && !t[i].literal && t[i].text==",")) {
            if(start==i) return {};
            if(i-start>=3 && (t[start].text=="Vector3" || t[start].text=="Color") && t[start+1].text=="(" && t[i-1].text==")") {
                int balance=0; bool whole=true;
                for(auto n=start+1;n<i-1;++n) if(!t[n].literal) {
                    if(t[n].text=="(") ++balance;
                    if(t[n].text==")" && --balance==0) {whole=false;break;}
                }
                auto components=whole?arguments(t,start+2,i-1,level+1):std::vector<slot>{};
                const std::vector<std::string> names=t[start].text=="Vector3"?std::vector<std::string>{"X","Y","Z"}:std::vector<std::string>{"R","G","B","A"};
                if(components.size()==names.size()) {
                    for(std::size_t n=0;n<components.size();++n) {components[n].label=names[n];result.push_back(components[n]);}
                } else result.push_back({"Valor",t[start].begin,t[i-1].end,false});
            } else result.push_back({"Valor",t[start].begin,t[i-1].end,false});
            start=i+1;
        }
    }
    return result;
}
struct call_shape { std::string key; std::vector<slot> fields; };
call_shape shape(const std::string& source) {
    auto t=code_tokens(source);
    if(t.empty()) return {};
    std::vector<slot> fields;
    std::size_t first=0;
    int depth=0;
    for(std::size_t i=0;i<t.size();++i) {
        if(!t[i].literal && t[i].text=="(") ++depth;
        if(!t[i].literal && t[i].text==")") --depth;
        if(!depth && !t[i].literal && t[i].text=="=") {
            if(i==0 || i+1==t.size()) return {};
            first=i+1;
            if(t[0].text!="local") {
                if(i<3 || t[i-2].text!=".") return {};
                fields.push_back({"Alvo",t[0].begin,t[i-2].begin,false});
                fields.push_back({"Propriedade",t[i-1].begin,t[i-1].end,false});
                auto values=arguments(t,first,t.size());
                fields.insert(fields.end(),values.begin(),values.end());
                std::string key="property:";
                for(std::size_t n=0;n<i;++n) key+=t[n].text;
                return {key,fields};
            }
            fields.push_back({"Resultado",t[1].begin,t[i-1].end,false});
            break;
        }
    }
    if(t.back().text!=")") {
        std::string key;
        for(std::size_t i=first;i<t.size();++i) key+=t[i].text;
        if(fields.empty()) return {};
        if(t.size()>=first+3 && t[t.size()-2].text==".") {
            fields.push_back({"Alvo",t[first].begin,t[t.size()-2].begin,false});
            fields.push_back({"Propriedade",t.back().begin,t.back().end,false});
        } else fields.push_back({"Valor",t[first].begin,t.back().end,false});
        return {key,fields};
    }
    // Find the opening parenthesis of the outer call, skipping calls in receivers.
    depth=0; std::size_t open=t.size();
    for(std::size_t i=t.size();i-->first;) {
        if(t[i].literal) continue;
        if(t[i].text==")") ++depth;
        if(t[i].text=="(" && --depth==0) {open=i;break;}
    }
    if(open==t.size() || open==first) return {};
    std::string key;
    if(open>=first+3 && t[open-2].text==":") {
        key=":"+t[open-1].text;
        fields.push_back({"Alvo",t[first].begin,t[open-2].begin,false});
    } else for(std::size_t i=first;i<open;++i) key+=t[i].text;
    auto args=arguments(t,open+1,t.size()-1);
    fields.insert(fields.end(),args.begin(),args.end());
    return {key,fields};
}
}
const std::vector<command>& commands() {
    static const std::vector<command> catalog{
        {"Objeto", "Mover", "gameObject.transform:Translate(Vector3(0, 0, 0))", {}},
        {"Objeto", "Girar", "gameObject.transform:Rotate(Vector3(0, 0, 0))", {}},
        {"Objeto", "Encontrar objeto", "local objeto = GameObject.Find(\"Objeto\")", {"Nome ou ID"}},
        {"Objeto", "Obter componente", "local componente = gameObject:GetComponent(\"Transform\")", {"Tipo"}},
        {"Objeto", "Definir pai", "local resultado = gameObject:SetParent(nil, true)", {"Pai", "Preservar posição global"}},
        {"Objeto", "É descendente", "local resultado = gameObject:IsDescendantOf(outro)", {"Outro objeto"}},
        {"Objeto", "Está colidindo", "local resultado = gameObject:IsCollidingWith(outro)", {"Outro objeto"}},
        {"Objeto", "Está no gatilho", "local resultado = gameObject:IsOverlappingTrigger(outro)", {"Outro objeto"}},
        {"Objeto", "Adicionar HUD", "local hud = gameObject:AddHUD()", {}},
        {"Objeto", "Destruir corpo físico", "Destroy(gameObject:GetComponent(\"Rigidbody\"))", {"Corpo físico"}},
        {"Física", "Aplicar força", "gameObject:GetComponent(\"Rigidbody\"):AddForce(Vector3(0, 0, 0))", {}},
        {"Física", "Limitar movimento", "gameObject:GetComponent(\"Rigidbody\"):SetLinearFactor(Vector3(0, 0, 0))", {}},
        {"Física", "Limitar rotação", "gameObject:GetComponent(\"Rigidbody\"):SetAngularFactor(Vector3(0, 0, 0))", {}},
        {"Câmera", "Traçar raio", "local resultado = gameObject:GetComponent(\"Camera\"):traceRay(100, nil, 0.5, 0.5)", {"Distância", "Ignorar", "Tela X", "Tela Y"}},
        {"Câmera", "Mundo para tela", "local resultado = gameObject:GetComponent(\"Camera\"):worldToScreen(Vector3(0, 0, 0))", {}},
        {"Câmera", "Tela para mundo", "local resultado = gameObject:GetComponent(\"Camera\"):screenToWorld(Vector3(0, 0, 0))", {}},
        {"Luzes", "Direção global", "local resultado = gameObject:GetComponent(\"SpotLight\"):GetWorldDirection()", {"Ângulo"}},
        {"Luzes", "Cone interno", "gameObject:GetComponent(\"SpotLight\"):SetInnerCutoff(12.5)", {"Ângulo"}},
        {"Luzes", "Cone externo", "gameObject:GetComponent(\"SpotLight\"):SetOuterCutoff(17.5)", {"Ângulo"}},
        {"Navegação", "Configurar navegação", "local resultado = gameObject:GetComponent(\"Pathfinder\"):Configure(1, 0.5, 20)", {"Célula", "Raio do agente", "Raio de busca"}},
        {"Navegação", "Buscar caminho", "local resultado = gameObject:GetComponent(\"Pathfinder\"):FindPath(Vector3(0, 0, 0))", {}},
        {"Navegação", "Obter caminho", "local resultado = gameObject:GetComponent(\"Pathfinder\"):GetPath()", {}},
        {"Navegação", "Limpar caminho", "gameObject:GetComponent(\"Pathfinder\"):Clear()", {}},
        {"Animação", "Reproduzir animação", "local resultado = gameObject:GetComponent(\"Animation\"):Play(\"\")", {"Arquivo .banim"}},
        {"Animação", "Pausar animação", "local resultado = gameObject:GetComponent(\"Animation\"):Pause()", {}},
        {"Animação", "Parar animação", "local resultado = gameObject:GetComponent(\"Animation\"):Stop()", {}},
        {"Animação", "Ir ao instante", "local resultado = gameObject:GetComponent(\"Animation\"):Seek(0)", {"Segundos"}},
        {"Animação", "Velocidade de animação", "local resultado = gameObject:GetComponent(\"Animation\"):SetSpeed(1)", {"Multiplicador"}},
        {"Animação", "Transição de animação", "local resultado = gameObject:GetComponent(\"Animation\"):CrossFade(\"\", 0.3)", {"Arquivo .banim", "Duração"}},
        {"Interface", "Adicionar elemento", "local elemento = gameObject:GetComponent(\"HUD\"):Add(\"text\", \"Texto\", -1)", {"Tipo", "Texto", "Índice do pai (-1 para raiz)"}},
        {"Interface", "Obter elemento", "local elemento = gameObject:GetComponent(\"HUD\"):Get(0)", {"Índice"}},
        {"Interface", "Posicionar elemento", "elemento:SetScreenPosition(Vector3(0, 0, 0))", {}},
        {"Interface", "Consumir clique", "local clicou = elemento:ConsumeClick()", {}},
        {"Entrada", "Tecla pressionada", "local resultado = Input.GetKey(\"W\")", {"Tecla"}},
        {"Entrada", "Ler eixo", "local resultado = Input.GetAxis(\"Horizontal\")", {"Eixo"}},
        {"Entrada", "Movimento do mouse", "local resultado = Input.GetMouseDelta()", {}},
        {"Entrada", "Capturar mouse", "Input.SetMouseCaptured(true)", {"Capturado"}},
        {"Scenes", "Trocar cena", "local resultado = SceneManager.LoadScene(\"cena.scene\")", {"Caminho"}},
        {"Scenes", "Recarregar cena", "local resultado = SceneManager.ReloadScene()", {}},
        {"Scenes", "Cena atual", "local resultado = SceneManager.GetActiveScene()", {}},
        {"Scenes", "Erro de cena", "local resultado = SceneManager.GetLastError()", {}},
        {"Scenes", "Cena carregando", "local resultado = SceneManager.IsLoading()", {}},
        {"Debug", "Registrar mensagem", "Debug.Log(\"Mensagem\")", {"Mensagem"}},
        {"Debug", "Imprimir", "print(\"Mensagem\")", {"Mensagem"}},
        {"Time", "Tempo entre quadros", "local tempo = Time.deltaTime", {}},
        {"Time", "Tempo decorrido", "local tempo = Time.time", {}},
        {"Matemática", "Criar vetor", "local resultado = Vector3(0, 0, 0)", {"X", "Y", "Z"}},
        {"Matemática", "Criar cor", "local resultado = Color(1, 1, 1, 1)", {"R", "G", "B", "A"}},
        {"Matemática", "Distância", "local resultado = Vector3.distance(a, b)", {"A", "B"}},
        {"Matemática", "Interpolar vetor", "local resultado = Vector3.lerp(a, b, 0.5)", {"A", "B", "Fator"}},
        {"Matemática", "Produto escalar", "local resultado = Vector3.dot(a, b)", {"A", "B"}},
        {"Matemática", "Produto vetorial", "local resultado = Vector3.cross(a, b)", {"A", "B"}},
        {"Matemática", "Comprimento", "local resultado = vetor:length()", {}},
        {"Matemática", "Normalizar", "local resultado = vetor:normalized()", {}},
        {"Propriedades", "Ler GameObject.id", "local valor = gameObject.id", {}},
        {"Propriedades", "Ler GameObject.transform", "local valor = gameObject.transform", {}},
        {"Propriedades", "Ler Transform.position", "local valor = gameObject.transform.position", {}},
        {"Propriedades", "Definir Transform.position", "gameObject.transform.position = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Transform.localPosition", "local valor = gameObject.transform.localPosition", {}},
        {"Propriedades", "Definir Transform.localPosition", "gameObject.transform.localPosition = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Transform.eulerAngles", "local valor = gameObject.transform.eulerAngles", {}},
        {"Propriedades", "Definir Transform.eulerAngles", "gameObject.transform.eulerAngles = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Transform.localEulerAngles", "local valor = gameObject.transform.localEulerAngles", {}},
        {"Propriedades", "Definir Transform.localEulerAngles", "gameObject.transform.localEulerAngles = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Transform.localScale", "local valor = gameObject.transform.localScale", {}},
        {"Propriedades", "Definir Transform.localScale", "gameObject.transform.localScale = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Renderer.enabled", "local valor = gameObject:GetComponent(\"Renderer\").enabled", {}},
        {"Propriedades", "Definir Renderer.enabled", "gameObject:GetComponent(\"Renderer\").enabled = true", {"Valor"}},
        {"Propriedades", "Ler Renderer.visible", "local valor = gameObject:GetComponent(\"Renderer\").visible", {}},
        {"Propriedades", "Definir Renderer.visible", "gameObject:GetComponent(\"Renderer\").visible = true", {"Valor"}},
        {"Propriedades", "Ler Renderer.castShadows", "local valor = gameObject:GetComponent(\"Renderer\").castShadows", {}},
        {"Propriedades", "Definir Renderer.castShadows", "gameObject:GetComponent(\"Renderer\").castShadows = true", {"Valor"}},
        {"Propriedades", "Ler Renderer.receiveShadows", "local valor = gameObject:GetComponent(\"Renderer\").receiveShadows", {}},
        {"Propriedades", "Definir Renderer.receiveShadows", "gameObject:GetComponent(\"Renderer\").receiveShadows = true", {"Valor"}},
        {"Propriedades", "Ler Rigidbody.mass", "local valor = gameObject:GetComponent(\"Rigidbody\").mass", {}},
        {"Propriedades", "Definir Rigidbody.mass", "gameObject:GetComponent(\"Rigidbody\").mass = 1", {"Valor"}},
        {"Propriedades", "Ler Rigidbody.friction", "local valor = gameObject:GetComponent(\"Rigidbody\").friction", {}},
        {"Propriedades", "Definir Rigidbody.friction", "gameObject:GetComponent(\"Rigidbody\").friction = 1", {"Valor"}},
        {"Propriedades", "Ler Rigidbody.restitution", "local valor = gameObject:GetComponent(\"Rigidbody\").restitution", {}},
        {"Propriedades", "Definir Rigidbody.restitution", "gameObject:GetComponent(\"Rigidbody\").restitution = 1", {"Valor"}},
        {"Propriedades", "Ler Rigidbody.velocity", "local valor = gameObject:GetComponent(\"Rigidbody\").velocity", {}},
        {"Propriedades", "Definir Rigidbody.velocity", "gameObject:GetComponent(\"Rigidbody\").velocity = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler Camera.viewportWidth", "local valor = gameObject:GetComponent(\"Camera\").viewportWidth", {}},
        {"Propriedades", "Ler Camera.viewportHeight", "local valor = gameObject:GetComponent(\"Camera\").viewportHeight", {}},
        {"Propriedades", "Ler Camera.forward", "local valor = gameObject:GetComponent(\"Camera\").forward", {}},
        {"Propriedades", "Ler Camera.aspect", "local valor = gameObject:GetComponent(\"Camera\").aspect", {}},
        {"Propriedades", "Ler Camera.fieldOfView", "local valor = gameObject:GetComponent(\"Camera\").fieldOfView", {}},
        {"Propriedades", "Definir Camera.fieldOfView", "gameObject:GetComponent(\"Camera\").fieldOfView = 1", {"Valor"}},
        {"Propriedades", "Ler Camera.nearClipPlane", "local valor = gameObject:GetComponent(\"Camera\").nearClipPlane", {}},
        {"Propriedades", "Definir Camera.nearClipPlane", "gameObject:GetComponent(\"Camera\").nearClipPlane = 1", {"Valor"}},
        {"Propriedades", "Ler Camera.farClipPlane", "local valor = gameObject:GetComponent(\"Camera\").farClipPlane", {}},
        {"Propriedades", "Definir Camera.farClipPlane", "gameObject:GetComponent(\"Camera\").farClipPlane = 1", {"Valor"}},
        {"Propriedades", "Ler Camera.orthographic", "local valor = gameObject:GetComponent(\"Camera\").orthographic", {}},
        {"Propriedades", "Definir Camera.orthographic", "gameObject:GetComponent(\"Camera\").orthographic = true", {"Valor"}},
        {"Propriedades", "Ler Camera.orthographicSize", "local valor = gameObject:GetComponent(\"Camera\").orthographicSize", {}},
        {"Propriedades", "Definir Camera.orthographicSize", "gameObject:GetComponent(\"Camera\").orthographicSize = 1", {"Valor"}},
        {"Propriedades", "Ler Camera.backgroundColor", "local valor = gameObject:GetComponent(\"Camera\").backgroundColor", {}},
        {"Propriedades", "Definir Camera.backgroundColor", "gameObject:GetComponent(\"Camera\").backgroundColor = Color(0.16, 0.16, 0.16, 1)", {"Valor"}},
        {"Propriedades", "Ler Camera.useSkybox", "local valor = gameObject:GetComponent(\"Camera\").useSkybox", {}},
        {"Propriedades", "Definir Camera.useSkybox", "gameObject:GetComponent(\"Camera\").useSkybox = true", {"Valor"}},
        {"Propriedades", "Ler Camera.skyboxImage", "local valor = gameObject:GetComponent(\"Camera\").skyboxImage", {}},
        {"Propriedades", "Definir Camera.skyboxImage", "gameObject:GetComponent(\"Camera\").skyboxImage = \"\"", {"Valor"}},
        {"Propriedades", "Ler Camera.fogEnabled", "local valor = gameObject:GetComponent(\"Camera\").fogEnabled", {}},
        {"Propriedades", "Definir Camera.fogEnabled", "gameObject:GetComponent(\"Camera\").fogEnabled = true", {"Valor"}},
        {"Propriedades", "Ler Camera.fogColor", "local valor = gameObject:GetComponent(\"Camera\").fogColor", {}},
        {"Propriedades", "Definir Camera.fogColor", "gameObject:GetComponent(\"Camera\").fogColor = Color(1, 1, 1, 1)", {"Valor"}},
        {"Propriedades", "Ler Camera.fogDensity", "local valor = gameObject:GetComponent(\"Camera\").fogDensity", {}},
        {"Propriedades", "Definir Camera.fogDensity", "gameObject:GetComponent(\"Camera\").fogDensity = 1", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.direction", "local valor = gameObject:GetComponent(\"DirectionalLight\").direction", {}},
        {"Propriedades", "Definir DirectionalLight.direction", "gameObject:GetComponent(\"DirectionalLight\").direction = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.ambient", "local valor = gameObject:GetComponent(\"DirectionalLight\").ambient", {}},
        {"Propriedades", "Definir DirectionalLight.ambient", "gameObject:GetComponent(\"DirectionalLight\").ambient = Vector3(1, 1, 1)", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.color", "local valor = gameObject:GetComponent(\"DirectionalLight\").color", {}},
        {"Propriedades", "Definir DirectionalLight.color", "gameObject:GetComponent(\"DirectionalLight\").color = Vector3(1, 1, 1)", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.intensity", "local valor = gameObject:GetComponent(\"DirectionalLight\").intensity", {}},
        {"Propriedades", "Definir DirectionalLight.intensity", "gameObject:GetComponent(\"DirectionalLight\").intensity = 1", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.shadows", "local valor = gameObject:GetComponent(\"DirectionalLight\").shadows", {}},
        {"Propriedades", "Definir DirectionalLight.shadows", "gameObject:GetComponent(\"DirectionalLight\").shadows = true", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.shadowDistance", "local valor = gameObject:GetComponent(\"DirectionalLight\").shadowDistance", {}},
        {"Propriedades", "Definir DirectionalLight.shadowDistance", "gameObject:GetComponent(\"DirectionalLight\").shadowDistance = 1", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.shadowBias", "local valor = gameObject:GetComponent(\"DirectionalLight\").shadowBias", {}},
        {"Propriedades", "Definir DirectionalLight.shadowBias", "gameObject:GetComponent(\"DirectionalLight\").shadowBias = 1", {"Valor"}},
        {"Propriedades", "Ler DirectionalLight.shadowStrength", "local valor = gameObject:GetComponent(\"DirectionalLight\").shadowStrength", {}},
        {"Propriedades", "Definir DirectionalLight.shadowStrength", "gameObject:GetComponent(\"DirectionalLight\").shadowStrength = 1", {"Valor"}},
        {"Propriedades", "Ler PointLight.position", "local valor = gameObject:GetComponent(\"PointLight\").position", {}},
        {"Propriedades", "Definir PointLight.position", "gameObject:GetComponent(\"PointLight\").position = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler PointLight.color", "local valor = gameObject:GetComponent(\"PointLight\").color", {}},
        {"Propriedades", "Definir PointLight.color", "gameObject:GetComponent(\"PointLight\").color = Vector3(1, 1, 1)", {"Valor"}},
        {"Propriedades", "Ler PointLight.intensity", "local valor = gameObject:GetComponent(\"PointLight\").intensity", {}},
        {"Propriedades", "Definir PointLight.intensity", "gameObject:GetComponent(\"PointLight\").intensity = 1", {"Valor"}},
        {"Propriedades", "Ler PointLight.constant", "local valor = gameObject:GetComponent(\"PointLight\").constant", {}},
        {"Propriedades", "Definir PointLight.constant", "gameObject:GetComponent(\"PointLight\").constant = 1", {"Valor"}},
        {"Propriedades", "Ler PointLight.linear", "local valor = gameObject:GetComponent(\"PointLight\").linear", {}},
        {"Propriedades", "Definir PointLight.linear", "gameObject:GetComponent(\"PointLight\").linear = 1", {"Valor"}},
        {"Propriedades", "Ler PointLight.quadratic", "local valor = gameObject:GetComponent(\"PointLight\").quadratic", {}},
        {"Propriedades", "Definir PointLight.quadratic", "gameObject:GetComponent(\"PointLight\").quadratic = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.worldPosition", "local valor = gameObject:GetComponent(\"SpotLight\").worldPosition", {}},
        {"Propriedades", "Ler SpotLight.worldDirection", "local valor = gameObject:GetComponent(\"SpotLight\").worldDirection", {}},
        {"Propriedades", "Ler SpotLight.position", "local valor = gameObject:GetComponent(\"SpotLight\").position", {}},
        {"Propriedades", "Definir SpotLight.position", "gameObject:GetComponent(\"SpotLight\").position = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler SpotLight.direction", "local valor = gameObject:GetComponent(\"SpotLight\").direction", {}},
        {"Propriedades", "Definir SpotLight.direction", "gameObject:GetComponent(\"SpotLight\").direction = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler SpotLight.color", "local valor = gameObject:GetComponent(\"SpotLight\").color", {}},
        {"Propriedades", "Definir SpotLight.color", "gameObject:GetComponent(\"SpotLight\").color = Vector3(1, 1, 1)", {"Valor"}},
        {"Propriedades", "Ler SpotLight.intensity", "local valor = gameObject:GetComponent(\"SpotLight\").intensity", {}},
        {"Propriedades", "Definir SpotLight.intensity", "gameObject:GetComponent(\"SpotLight\").intensity = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.constant", "local valor = gameObject:GetComponent(\"SpotLight\").constant", {}},
        {"Propriedades", "Definir SpotLight.constant", "gameObject:GetComponent(\"SpotLight\").constant = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.linear", "local valor = gameObject:GetComponent(\"SpotLight\").linear", {}},
        {"Propriedades", "Definir SpotLight.linear", "gameObject:GetComponent(\"SpotLight\").linear = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.quadratic", "local valor = gameObject:GetComponent(\"SpotLight\").quadratic", {}},
        {"Propriedades", "Definir SpotLight.quadratic", "gameObject:GetComponent(\"SpotLight\").quadratic = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.innerCutoff", "local valor = gameObject:GetComponent(\"SpotLight\").innerCutoff", {}},
        {"Propriedades", "Definir SpotLight.innerCutoff", "gameObject:GetComponent(\"SpotLight\").innerCutoff = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.outerCutoff", "local valor = gameObject:GetComponent(\"SpotLight\").outerCutoff", {}},
        {"Propriedades", "Definir SpotLight.outerCutoff", "gameObject:GetComponent(\"SpotLight\").outerCutoff = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.shadows", "local valor = gameObject:GetComponent(\"SpotLight\").shadows", {}},
        {"Propriedades", "Definir SpotLight.shadows", "gameObject:GetComponent(\"SpotLight\").shadows = true", {"Valor"}},
        {"Propriedades", "Ler SpotLight.shadowDistance", "local valor = gameObject:GetComponent(\"SpotLight\").shadowDistance", {}},
        {"Propriedades", "Definir SpotLight.shadowDistance", "gameObject:GetComponent(\"SpotLight\").shadowDistance = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.shadowBias", "local valor = gameObject:GetComponent(\"SpotLight\").shadowBias", {}},
        {"Propriedades", "Definir SpotLight.shadowBias", "gameObject:GetComponent(\"SpotLight\").shadowBias = 1", {"Valor"}},
        {"Propriedades", "Ler SpotLight.shadowStrength", "local valor = gameObject:GetComponent(\"SpotLight\").shadowStrength", {}},
        {"Propriedades", "Definir SpotLight.shadowStrength", "gameObject:GetComponent(\"SpotLight\").shadowStrength = 1", {"Valor"}},
        {"Propriedades", "Ler SoundListener.enabled", "local valor = gameObject:GetComponent(\"SoundListener\").enabled", {}},
        {"Propriedades", "Definir SoundListener.enabled", "gameObject:GetComponent(\"SoundListener\").enabled = true", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.clip", "local valor = gameObject:GetComponent(\"SoundPlayer\").clip", {}},
        {"Propriedades", "Definir SoundPlayer.clip", "gameObject:GetComponent(\"SoundPlayer\").clip = \"\"", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.volume", "local valor = gameObject:GetComponent(\"SoundPlayer\").volume", {}},
        {"Propriedades", "Definir SoundPlayer.volume", "gameObject:GetComponent(\"SoundPlayer\").volume = 1", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.pitch", "local valor = gameObject:GetComponent(\"SoundPlayer\").pitch", {}},
        {"Propriedades", "Definir SoundPlayer.pitch", "gameObject:GetComponent(\"SoundPlayer\").pitch = 1", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.radius", "local valor = gameObject:GetComponent(\"SoundPlayer\").radius", {}},
        {"Propriedades", "Definir SoundPlayer.radius", "gameObject:GetComponent(\"SoundPlayer\").radius = 1", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.looping", "local valor = gameObject:GetComponent(\"SoundPlayer\").looping", {}},
        {"Propriedades", "Definir SoundPlayer.looping", "gameObject:GetComponent(\"SoundPlayer\").looping = true", {"Valor"}},
        {"Propriedades", "Ler SoundPlayer.playOnAwake", "local valor = gameObject:GetComponent(\"SoundPlayer\").playOnAwake", {}},
        {"Propriedades", "Definir SoundPlayer.playOnAwake", "gameObject:GetComponent(\"SoundPlayer\").playOnAwake = true", {"Valor"}},
        {"Propriedades", "Ler Animation.time", "local valor = gameObject:GetComponent(\"Animation\").time", {}},
        {"Propriedades", "Ler Animation.isPlaying", "local valor = gameObject:GetComponent(\"Animation\").isPlaying", {}},
        {"Propriedades", "Ler Animation.isBlending", "local valor = gameObject:GetComponent(\"Animation\").isBlending", {}},
        {"Propriedades", "Ler Animation.speed", "local valor = gameObject:GetComponent(\"Animation\").speed", {}},
        {"Propriedades", "Definir Animation.speed", "gameObject:GetComponent(\"Animation\").speed = 1", {"Valor"}},
        {"Propriedades", "Ler HUD.count", "local valor = gameObject:GetComponent(\"HUD\").count", {}},
        {"Propriedades", "Ler HUD.enabled", "local valor = gameObject:GetComponent(\"HUD\").enabled", {}},
        {"Propriedades", "Definir HUD.enabled", "gameObject:GetComponent(\"HUD\").enabled = true", {"Valor"}},
        {"Propriedades", "Ler HudElement.text", "local valor = elemento.text", {}},
        {"Propriedades", "Definir HudElement.text", "elemento.text = \"\"", {"Valor"}},
        {"Propriedades", "Ler HudElement.width", "local valor = elemento.width", {}},
        {"Propriedades", "Definir HudElement.width", "elemento.width = 1", {"Valor"}},
        {"Propriedades", "Ler HudElement.height", "local valor = elemento.height", {}},
        {"Propriedades", "Definir HudElement.height", "elemento.height = 1", {"Valor"}},
        {"Propriedades", "Ler HudElement.scale", "local valor = elemento.scale", {}},
        {"Propriedades", "Definir HudElement.scale", "elemento.scale = 1", {"Valor"}},
        {"Propriedades", "Ler HudElement.visible", "local valor = elemento.visible", {}},
        {"Propriedades", "Definir HudElement.visible", "elemento.visible = true", {"Valor"}},
        {"Propriedades", "Ler HudElement.position", "local valor = elemento.position", {}},
        {"Propriedades", "Definir HudElement.position", "elemento.position = Vector3(0, 0, 0)", {"Valor"}},
        {"Propriedades", "Ler HudElement.absolute", "local valor = elemento.absolute", {}},
        {"Propriedades", "Definir HudElement.absolute", "elemento.absolute = true", {"Valor"}},
        {"Propriedades", "Ler Vector3.x", "local valor = vetor.x", {}},
        {"Propriedades", "Definir Vector3.x", "vetor.x = 1", {"Valor"}},
        {"Propriedades", "Ler Vector3.y", "local valor = vetor.y", {}},
        {"Propriedades", "Definir Vector3.y", "vetor.y = 1", {"Valor"}},
        {"Propriedades", "Ler Vector3.z", "local valor = vetor.z", {}},
        {"Propriedades", "Definir Vector3.z", "vetor.z = 1", {"Valor"}},
        {"Propriedades", "Ler Color.r", "local valor = cor.r", {}},
        {"Propriedades", "Definir Color.r", "cor.r = 1", {"Valor"}},
        {"Propriedades", "Ler Color.g", "local valor = cor.g", {}},
        {"Propriedades", "Definir Color.g", "cor.g = 1", {"Valor"}},
        {"Propriedades", "Ler Color.b", "local valor = cor.b", {}},
        {"Propriedades", "Definir Color.b", "cor.b = 1", {"Valor"}},
        {"Propriedades", "Ler Color.a", "local valor = cor.a", {}},
        {"Propriedades", "Definir Color.a", "cor.a = 1", {"Valor"}},
        {"Propriedades", "Ler propriedade do script", "local valor = properties.nome", {}},
        {"Propriedades", "Definir propriedade do script", "properties.nome = 0", {"Valor"}},
        {"Compatibilidade", "Log antigo", "bubble.log(\"Mensagem\")", {"Mensagem"}},
        {"Compatibilidade", "Tecla antiga", "bubble.input.is_key_down(\"W\")", {"Tecla"}},
        {"Compatibilidade", "Mouse antigo", "bubble.input.mouse_delta()", {}},
        {"Compatibilidade", "Captura antiga", "bubble.input.set_mouse_captured(true)", {"Capturado"}},
        {"Compatibilidade", "Posição antiga", "bubble.transform.get_position()", {"ID"}},
        {"Compatibilidade", "Posição de entidade antiga", "local x, y, z = bubble.entity.get_position(bubble.meuID)", {"ID"}},
        {"Compatibilidade", "Visibilidade antiga", "bubble.entity.set_visible(bubble.meuID, true)", {"ID", "Visível"}},
        {"Compatibilidade", "Remover física antiga", "bubble.entity.remove_physics(bubble.meuID)", {"ID"}},
        {"Compatibilidade", "Rotação antiga", "local x, y, z = bubble.transform.get_rotation()", {}},
        {"Compatibilidade", "Definir rotação antiga", "bubble.transform.set_rotation(0, 0, 0)", {"X", "Y", "Z"}},
        {"Compatibilidade", "Avançar antigo", "bubble.transform.move_forward(1)", {"Distância"}},
        {"Compatibilidade", "Mover lateral antigo", "bubble.transform.move_right(1)", {"Distância"}},
        {"Compatibilidade", "ID antigo", "local resultado = bubble.meuID", {}},
        {"Compatibilidade", "Tempo antigo", "local resultado = bubble.delta_time", {}},
        {"Eventos", "Quando iniciar", "function setup()\nend", {}},
        {"Eventos", "A cada quadro", "function update()\nend", {}},
    };
    return catalog;
}
const command* describe(const block& value) {
    if(value.type==kind::function) {
        const auto t=code_tokens(value.head);
        if(t.size()==4 && t[0].text=="function" && (t[1].text=="setup" || t[1].text=="update"))
            for(const auto& c:commands()) if(c.category=="Eventos" && c.source.starts_with("function "+t[1].text+"(")) return &c;
        return nullptr;
    }
    if(value.type!=kind::command && value.type!=kind::scene_load && value.type!=kind::assignment) return nullptr;
    const auto key=shape(value.head).key;
    if(key.empty()) return nullptr;
    static const auto keys=[] {
        std::vector<std::string> result;
        for(const auto& c:commands()) result.push_back(shape(c.source).key);
        return result;
    }();
    for(std::size_t i=0;i<keys.size();++i) if(keys[i]==key) return &commands()[i];
    const auto property=key.starts_with("property:properties.")?"Definir propriedade do script":key.starts_with("properties.")?"Ler propriedade do script":"";
    if(*property) for(const auto& c:commands()) if(c.title==property) return &c;
    return nullptr;
}
std::string title(const block& value) {
    if(const auto* c=describe(value)) return c->title;
    if(value.type==kind::assignment && shape(value.head).key.starts_with("property:")) return "Definir propriedade";
    const auto t=code_tokens(value.head);
    if(value.type==kind::raw && t.empty()) return "Comentários / propriedades";
    if(value.type==kind::command && !t.empty() && t[0].text=="return") return "Retornar";
    if(value.type==kind::function && t.size()>2) return "Função: "+t[t[0].text=="local"?2:1].text;
    if(value.type==kind::assignment && t.size()>1) {
        const auto first=t[0].text=="local"?1u:0u;
        return std::string(t.size()==2?"Declarar: ":"Calcular: ")+t[first].text;
    }
    return label(value.type);
}
std::vector<slot> slots(const block& b) {
    if(const auto* c=describe(b); c && b.type!=kind::function) {
        auto result=shape(b.head).fields; std::size_t n=0;
        for(auto& field:result) if(field.label=="Valor") {if(n<c->arguments.size()) field.label=c->arguments[n]; else field.label="Argumento "+std::to_string(n+1); ++n;}
        return result;
    }
    if(b.type==kind::assignment) {
        auto parsed=shape(b.head);
        if(parsed.key.starts_with("property:")) return parsed.fields;
    }
    const auto t=code_tokens(b.head);
    if(t.empty() || b.type==kind::raw) return {{"Lua",0,b.head.size(),false}};
    if(b.type==kind::condition || (b.type==kind::branch && t[0].text=="elseif") || b.type==kind::loop || b.type==kind::while_loop)
        return {{(b.type==kind::loop || b.type==kind::while_loop)?"Limites / condição":"Condição",t.size()>2?t[1].begin:t[0].end,t.size()>2?t[t.size()-2].end:t.back().begin,false}};
    if(b.type==kind::function) {
        const auto start=t[0].text=="local" && t.size()>2?t[2].begin:(t.size()>1?t[1].begin:t[0].end);
        return {{"Nome e parâmetros",start,t.back().end,false}};
    }
    if(b.type==kind::repeat) {
        const auto tail=code_tokens(b.tail);
        if(tail.size()>1) return {{"Até",tail[1].begin,tail.back().end,true}};
        return {};
    }
    if(b.type==kind::branch || b.type==kind::scope) return {};
    if(t[0].text=="return") return t.size()==1?std::vector<slot>{}:std::vector<slot>{{"Valores",t[1].begin,t.back().end,false}};
    if(b.type==kind::assignment && t.size()==2 && t[0].text=="local") return {{"Variável",t[1].begin,t[1].end,false}};
    for(std::size_t i=1;i+1<t.size();++i) if(t[i].text=="=")
        return {{"Variável",t[0].text=="local"?t[1].begin:t[0].begin,t[i-1].end,false},{"Valor",t[i+1].begin,t.back().end,false}};
    if(t.back().text==")") {
        auto paren=std::find_if(t.begin(),t.end(),[](const token& v){return v.text=="(";});
        if(paren!=t.end()) return {{"Ação",t[0].begin,paren->begin,false},{"Argumentos",paren->end,t.back().begin,false}};
    }
    return {{"Comando",t[0].begin,t.back().end,false}};
}
void document::load(const std::string& source) { m_blocks=parse(source); m_undo.clear(); m_redo.clear(); }
const block* document::find(std::uint64_t id) const { return find_block(m_blocks,id); }
void document::remember(const std::string& previous) {
    if(previous==source()) return;
    m_undo.push_back(previous); if(m_undo.size()>100) m_undo.erase(m_undo.begin()); m_redo.clear();
}
bool document::replace(std::uint64_t id,const std::string& head,const std::string& tail) {
    auto* b=find_block(m_blocks,id); if(!b) return false;
    const auto previous=source(); b->head=head; b->tail=tail; remember(previous); return true;
}
bool document::edit(std::uint64_t id,std::size_t index,const std::string& value) {
    auto* b=find_block(m_blocks,id); if(!b) return false;
    const auto fields=slots(*b); if(index>=fields.size()) return false;
    const auto field=fields[index]; const auto previous=source();
    auto& text=field.tail?b->tail:b->head;
    text.replace(field.begin,field.end-field.begin,value); remember(previous); return true;
}
bool document::insert(std::uint64_t parent,kind type,std::uint64_t before_id) {
    auto* p=parent?find_block(m_blocks,parent):nullptr;
    if(parent && (!p || !container(p->type))) return false;
    if(type==kind::branch && (!p || p->type!=kind::condition)) return false;
    if(type==kind::branch && std::any_of(p->children.begin(),p->children.end(),[](const block& b){const auto t=code_tokens(b.head);return b.type==kind::branch && !t.empty() && t[0].text=="else";})) return false;
    const auto& destination=p?p->children:m_blocks;
    if(before_id && std::none_of(destination.begin(),destination.end(),[&](const block& b){return b.id==before_id;})) return false;
    const auto previous=source(); const auto nl=previous.find("\r\n")!=std::string::npos?"\r\n":"\n";
    std::uint64_t id=1;
    auto scan=[&](auto&& self,const std::vector<block>& values)->void { for(const auto& b:values) {id=std::max(id,b.id+1); self(self,b.children);} }; scan(scan,m_blocks);
    auto& values=p?p->children:m_blocks; terminate(values,nl);
    const auto before=type==kind::branch?values.end():std::find_if(values.begin(),values.end(),[&](const block& b){return before_id?b.id==before_id:b.type==kind::branch;});
    values.insert(before,make(type,id,nl)); remember(previous); return true;
}
bool document::insert_command(std::uint64_t parent, std::size_t index, std::uint64_t before) {
    if(index>=commands().size()) return false;
    auto* p=parent?find_block(m_blocks,parent):nullptr;
    if(parent && (!p || !container(p->type))) return false;
    auto& values=p?p->children:m_blocks;
    if(before && std::none_of(values.begin(),values.end(),[&](const block& b){return b.id==before;})) return false;
    const auto previous=source();
    const std::string nl=previous.find("\r\n")!=std::string::npos?"\r\n":"\n";
    auto added=parse(commands()[index].source+nl);
    if(added.size()!=1) return false;
    std::uint64_t id=1;
    auto scan=[&](auto&& self,const std::vector<block>& list)->void {for(const auto& b:list){id=std::max(id,b.id+1);self(self,b.children);}};
    scan(scan,m_blocks); added[0].id=id;
    terminate(values,nl);
    auto at=std::find_if(values.begin(),values.end(),[&](const block& b){return before?b.id==before:b.type==kind::branch;});
    values.insert(at,std::move(added[0]));remember(previous);return true;
}
std::string document::copy(std::uint64_t id) const {
    const auto* b=find(id); return b?emit({*b}):std::string{};
}
bool document::paste(std::uint64_t parent, const std::string& text, std::uint64_t before, std::string* error) {
    if(error) error->clear();
    if(text.empty()) return false;
    auto* p=parent?find_block(m_blocks,parent):nullptr;
    if(parent && (!p || !container(p->type))) return false;
    auto& values=p?p->children:m_blocks;
    if(before && std::none_of(values.begin(),values.end(),[&](const block& b){return b.id==before;})) return false;
    auto added=parse(text);
    if(added.empty()) return false;
    const auto previous=source();
    const std::string nl=previous.find("\r\n")!=std::string::npos?"\r\n":"\n";
    std::uint64_t next=1;
    auto scan=[&](auto&& self,const std::vector<block>& list)->void {for(const auto& b:list){next=std::max(next,b.id+1);self(self,b.children);}};
    scan(scan,m_blocks);
    auto identify=[&](auto&& self,std::vector<block>& list)->void {for(auto& b:list){b.id=next++;self(self,b.children);}};
    const auto previous_blocks=m_blocks;
    identify(identify,added);terminate(values,nl);terminate(added,nl);
    auto at=std::find_if(values.begin(),values.end(),[&](const block& b){return before?b.id==before:b.type==kind::branch;});
    values.insert(at,std::make_move_iterator(added.begin()),std::make_move_iterator(added.end()));
    std::string diagnostic;
    if(!validate(source(),diagnostic)) {m_blocks=previous_blocks;if(error) *error=diagnostic;return false;}
    remember(previous);return true;
}
bool document::erase(std::uint64_t id) {
    auto* list=parent_list(m_blocks,id); if(!list) return false;
    const auto previous=source(); std::erase_if(*list,[&](const block& b){return b.id==id;}); remember(previous); return true;
}
bool document::move(std::uint64_t id,std::uint64_t parent,std::uint64_t before) {
    auto* b=find_block(m_blocks,id); auto* p=parent?find_block(m_blocks,parent):nullptr;
    if(!b || id==parent || id==before || (parent && (!p || !container(p->type))) || find_block(b->children,parent) || find_block(b->children,before)) return false;
    if(b->type==kind::branch && (!p || p->type!=kind::condition)) return false;
    auto* dest=p?&p->children:&m_blocks;
    if(before && std::none_of(dest->begin(),dest->end(),[&](const block& n){return n.id==before;})) return false;
    const auto previous=source(); block value=*b;
    auto* old=parent_list(m_blocks,id); std::erase_if(*old,[&](const block& n){return n.id==id;});
    // Erasing can relocate vector entries; resolve the destination again.
    p=parent?find_block(m_blocks,parent):nullptr; dest=p?&p->children:&m_blocks;
    const auto nl=previous.find("\r\n")!=std::string::npos?"\r\n":"\n";
    terminate(*dest,nl);
    if(!value.tail.empty() && value.tail.back()!='\n') value.tail+=nl;
    else if(value.tail.empty() && !value.head.empty() && value.head.back()!='\n') value.head+=nl;
    auto at=std::find_if(dest->begin(),dest->end(),[&](const block& n){return n.id==before;});
    if(!before && value.type!=kind::branch) at=std::find_if(dest->begin(),dest->end(),[](const block& n){return n.type==kind::branch;});
    dest->insert(at,std::move(value)); remember(previous); return true;
}
bool document::undo() { if(m_undo.empty()) return false; m_redo.push_back(source()); m_blocks=parse(m_undo.back()); m_undo.pop_back(); return true; }
bool document::redo() { if(m_redo.empty()) return false; m_undo.push_back(source()); m_blocks=parse(m_redo.back()); m_redo.pop_back(); return true; }
}

#include "glad.h"
#include "debugging/debug.hpp"
#include "components/camera.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include <cmath>
#include <unordered_set>

using namespace COMMONS_NS;

/**
 * @brief Ativa a escrita do framebuffer da câmera
 */

bgui::vec3 normalized(const bgui::vec3& v) {
    float length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0) return {0.f, 0.f, 0.f};
    return {v.x / length, v.y / length, v.z / length};
}

void camera::request_framebuffer_size(const bgui::vec2i& size) {
    if (size.x > 0 && size.y > 0)
        m_requested_framebuffer_size = size;
}

void camera::drawFB() {
    if (m_requested_framebuffer_size.x > 0 && m_requested_framebuffer_size.y > 0) {
        viewport(m_requested_framebuffer_size);
        if (!flag_fb) createFB();
        m_requested_framebuffer_size = {0, 0};
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    if (flag_fb) {
        if (viewport_ptr)
            viewport(*viewport_ptr);

        if (viewportFBO.x <= 0 || viewportFBO.y <= 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, viewportFBO.x, viewportFBO.y);

        glClearColor(config.sky_solid_color.r, config.sky_solid_color.g,
            config.sky_solid_color.b, config.sky_solid_color.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(config.sky_solid_color.r, config.sky_solid_color.g,
        config.sky_solid_color.b, config.sky_solid_color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


camera::~camera()
{
    debugging::emit(debug, "camera", "descarregando");

    release_graphics_resources();
}

void camera::release_graphics_resources() {
    delete m_skybox;
    m_skybox = nullptr;
    deleteFB();
}

camera::camera(const bool orth)
	: config{} {
	config.flag_orth = orth;
}

bool camera_post_processing::valid() const {
    return occlusion >= ambient_occlusion_mode::off && occlusion <= ambient_occlusion_mode::gtao &&
        std::isfinite(radius) && radius >= min_radius && radius <= max_radius &&
        std::isfinite(strength) && strength >= 0.f && strength <= 1.f;
}

bool camera::analyze(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    camera_config next = config;
    next.post_processing = {};
    if (value.HasMember("post_processing")) {
        const auto& post = value["post_processing"];
        if (!post.IsObject() || !post.HasMember("occlusion") || !post["occlusion"].IsUint() ||
            post["occlusion"].GetUint() > static_cast<unsigned>(ambient_occlusion_mode::gtao) ||
            !post.HasMember("radius") || !post["radius"].IsNumber() ||
            !post.HasMember("strength") || !post["strength"].IsNumber())
            return false;
        next.post_processing = {static_cast<ambient_occlusion_mode>(post["occlusion"].GetUint()),
            post["radius"].GetFloat(), post["strength"].GetFloat()};
        if (!next.post_processing.valid()) return false;
    }
    next.use_skybox = false;
    next.skybox_image.clear();
    next.fog_enabled = false;
    next.fog_density = 0.01f;
    next.fog_color = {0.43f, 0.78f, 0.86f, 1.f};
    auto next_skybox_faces = skybox_faces;
    const float near_clip = value.HasMember("znear")
        ? (value["znear"].IsNumber() ? value["znear"].GetFloat() : 0.f)
        : config.corte_curto;
    const float far_clip = value.HasMember("zfar")
        ? (value["zfar"].IsNumber() ? value["zfar"].GetFloat() : 0.f)
        : config.corte_longo;
    if (!std::isfinite(near_clip) || !std::isfinite(far_clip) ||
        near_clip <= 0.f || far_clip <= near_clip)
        return false;

    const auto read_float = [&](const char* name, float& output) {
        if (!value.HasMember(name)) return true;
        if (!value[name].IsNumber()) return false;
        const float parsed = value[name].GetFloat();
        if (!std::isfinite(parsed)) return false;
        output = parsed;
        return true;
    };
    const auto read_bool = [&](const char* name, bool& output) {
        if (!value.HasMember(name)) return true;
        if (!value[name].IsBool()) return false;
        output = value[name].GetBool();
        return true;
    };
    if (!read_float("fov", next.fov) || !read_float("scale", next.scale) ||
        !read_float("fog_density", next.fog_density) ||
        !read_bool("ortho", next.flag_orth) || !read_bool("skybox", next.use_skybox) ||
        !read_bool("fog_enabled", next.fog_enabled))
        return false;
    next.corte_curto = near_clip;
    next.corte_longo = far_clip;
    if (next.fov < 1.f || next.fov > 179.f || next.scale <= 0.f || next.fog_density < 0.f)
        return false;
	if (value.HasMember("ceu")) {
		const auto& sky_color = value["ceu"];
		if (!sky_color.IsArray() || sky_color.Size() != 4) return false;
        for (rapidjson::SizeType index = 0; index < 4; ++index)
            if (!sky_color[index].IsNumber() || !std::isfinite(sky_color[index].GetFloat()) ||
                sky_color[index].GetFloat() < 0.f || sky_color[index].GetFloat() > 255.f) return false;
		next.sky_solid_color = {
			sky_color[0].GetFloat() / 255.f, sky_color[1].GetFloat() / 255.f,
			sky_color[2].GetFloat() / 255.f, sky_color[3].GetFloat() / 255.f
		};
	}
    if (value.HasMember("fog_color")) {
        const auto& fog_color = value["fog_color"];
        if (!fog_color.IsArray() || (fog_color.Size() != 3 && fog_color.Size() != 4)) return false;
        for (rapidjson::SizeType index = 0; index < fog_color.Size(); ++index)
            if (!fog_color[index].IsNumber() || !std::isfinite(fog_color[index].GetFloat()) ||
                fog_color[index].GetFloat() < 0.f || fog_color[index].GetFloat() > 1.f) return false;
        next.fog_color = {
            fog_color[0].GetFloat(), fog_color[1].GetFloat(), fog_color[2].GetFloat(),
            fog_color.Size() == 4 ? fog_color[3].GetFloat() : 1.f
        };
	}
    if (value.HasMember("skybox_image") && (!value["skybox_image"].IsString() ||
        value["skybox_image"].GetStringLength() != std::char_traits<char>::length(value["skybox_image"].GetString()))) return false;
    if (value.HasMember("skybox_image")) next.skybox_image = value["skybox_image"].GetString();
    next_skybox_faces = {};
    if (value.HasMember("skybox_faces")) {
        const auto& faces = value["skybox_faces"];
        if (!faces.IsArray() || faces.Size() != 6) return false;
        for (unsigned i = 0; i < 6; ++i) {
            if (!faces[i].IsString()) return false;
            next_skybox_faces[i] = faces[i].GetString();
        }
    }
    if (value.HasMember("atmosphere_zenith")) {
        const auto& c = value["atmosphere_zenith"];
        if (!c.IsArray() || c.Size() != 3) return false;
        for (unsigned i = 0; i < 3; ++i) {
            if (!c[i].IsNumber() || !std::isfinite(c[i].GetFloat())) return false;
            next.atmosphere_zenith[i] = c[i].GetFloat();
        }
    }
	config = std::move(next);
	skybox_faces = std::move(next_skybox_faces);
	return true;
}

void camera::createFB() {
    if (flag_fb)
        return;

    if (viewportFBO.x <= 0 || viewportFBO.y <= 0)
        return;

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texture);
    glGenRenderbuffers(1, &rbo);

    flag_fb = true;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        viewportFBO.x,
        viewportFBO.y,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        texture,
        0
    );

    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        viewportFBO.x,
        viewportFBO.y
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        rbo
    );

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteFB();
        return;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void camera::viewport(const bgui::vec2i& viewp) {
    if (viewp.x <= 0 || viewp.y <= 0)
        return;

    if (viewportFBO.x == viewp.x && viewportFBO.y == viewp.y)
        return;

    if (!flag_fb) {
        viewportFBO = viewp;
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        viewp.x,
        viewp.y,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        nullptr
    );
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        viewp.x,
        viewp.y
    );
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        debugging::emit(
            debug,
            "camera",
            "Framebuffer incompleto apos redimensionamento"
        );
        return;
    }

    viewportFBO = viewp;
}
bool camera::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const
{
    const auto valid_channel = [](float channel) {
        return std::isfinite(channel) && channel >= 0.f && channel <= 1.f;
    };
    if (!value.IsObject() || !config.post_processing.valid() ||
        config.skybox_image.find('\0') != std::string::npos ||
        !std::isfinite(config.fov) || config.fov < 1.f || config.fov > 179.f ||
        !std::isfinite(config.corte_curto) || !std::isfinite(config.corte_longo) ||
        !std::isfinite(config.scale) || config.scale <= 0.f ||
        !std::isfinite(config.fog_density) || config.fog_density < 0.f ||
        config.corte_curto <= 0.f || config.corte_longo <= config.corte_curto ||
        !valid_channel(config.sky_solid_color.r) || !valid_channel(config.sky_solid_color.g) ||
        !valid_channel(config.sky_solid_color.b) || !valid_channel(config.sky_solid_color.a) ||
        !valid_channel(config.fog_color.r) || !valid_channel(config.fog_color.g) ||
        !valid_channel(config.fog_color.b) || !valid_channel(config.fog_color.a))
        return false;
    for (unsigned index = 0; index < 3; ++index)
        if (!std::isfinite(config.atmosphere_zenith[index])) return false;
    rapidjson::Value post(rapidjson::kObjectType);
    post.AddMember("occlusion", static_cast<unsigned>(config.post_processing.occlusion), allocator);
    post.AddMember("radius", config.post_processing.radius, allocator);
    post.AddMember("strength", config.post_processing.strength, allocator);
    value.AddMember("post_processing", post, allocator);

    // fov
    value.AddMember("fov", config.fov, allocator);

    // zfar
    value.AddMember("zfar", config.corte_longo, allocator);
    value.AddMember("znear", config.corte_curto, allocator);

    // scale
    value.AddMember("scale", config.scale, allocator);

    // ortho flag
    value.AddMember("ortho", config.flag_orth, allocator);

    // ceu (vector RGBA)
    rapidjson::Value cor_ceu(rapidjson::kArrayType);
    cor_ceu.PushBack(static_cast<int>(config.sky_solid_color.r * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(config.sky_solid_color.g * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(config.sky_solid_color.b * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(config.sky_solid_color.a * 255), allocator);
    value.AddMember("ceu", cor_ceu, allocator);

    value.AddMember("skybox_image", rapidjson::Value(config.skybox_image.c_str(), allocator), allocator);
    rapidjson::Value faces(rapidjson::kArrayType);
    for (const auto& path : skybox_faces)
        faces.PushBack(rapidjson::Value(path.c_str(), allocator), allocator);
    value.AddMember("skybox_faces", faces, allocator);
    rapidjson::Value zenith(rapidjson::kArrayType);
    for (unsigned i = 0; i < 3; ++i) zenith.PushBack(config.atmosphere_zenith[i], allocator);
    value.AddMember("atmosphere_zenith", zenith, allocator);
    // skybox (flag)
    value.AddMember("skybox", config.use_skybox, allocator);

    value.AddMember("fog_enabled", config.fog_enabled, allocator);
    value.AddMember("fog_density", config.fog_density, allocator);
    rapidjson::Value fog_color(rapidjson::kArrayType);
    fog_color.PushBack(config.fog_color.r, allocator);
    fog_color.PushBack(config.fog_color.g, allocator);
    fog_color.PushBack(config.fog_color.b, allocator);
    fog_color.PushBack(config.fog_color.a, allocator);
    value.AddMember("fog_color", fog_color, allocator);

    return true;
}

void camera::deleteFB()
{
    if (!flag_fb) return;
    flag_fb = false;

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    glDeleteRenderbuffers(1, &rbo);
    fbo = texture = rbo = 0;
}

glm::mat4 camera::getViewMatrix() {
    const auto registry = reg.lock();
    const auto owned_transform = registry ? registry->get<transform>(my_object) : nullptr;
    // Preserve explicit transforms on standalone cameras, but discard expired ECS caches.
    const std::weak_ptr<ecs> empty_registry;
    if (reg.owner_before(empty_registry) || empty_registry.owner_before(reg))
        m_transform = owned_transform.get();
    if (!m_transform)
        return viewMatrix;

    position = m_transform->get_world_position();
    const auto local_rotation = m_transform->get_rotation();
    const float yaw = glm::radians(local_rotation.y);
    const float pitch = glm::radians(local_rotation.x);
    glm::vec3 direction{std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                        std::sin(yaw) * std::cos(pitch)};
    // Compose ancestor rotations directly: Euler decomposition flips its pitch
    // and yaw representation at the poles. Ignore scale, including nonuniform scale.
    glm::quat inherited{1.f, 0.f, 0.f, 0.f};
    std::unordered_set<uint32_t> visited{m_transform->my_object};
    auto parent_id = m_transform->parent_id;
    while (parent_id && registry && visited.insert(parent_id).second) {
        auto parent = registry->get<transform>(parent_id);
        if (!parent) break;
        const auto r = parent->get_rotation();
        const auto q = glm::angleAxis(glm::radians(r.x), glm::vec3(1, 0, 0)) *
            glm::angleAxis(glm::radians(r.y), glm::vec3(0, 1, 0)) *
            glm::angleAxis(glm::radians(r.z), glm::vec3(0, 0, 1));
        inherited = q * inherited;
        parent_id = parent->parent_id;
    }
    direction = inherited * direction;
    const bgui::vec3 frente{direction.x, direction.y, direction.z};
    forward = normalized(frente);

    up = {0.f, 1.f, 0.f};

    const glm::vec3 glm_forward = glm::vec3(forward.x, forward.y, forward.z);
    const glm::vec3 glm_up = glm::vec3(up.x, up.y, up.z);
    const glm::vec3 glm_right = glm::normalize(glm::cross(glm_forward, glm_up));
    right = {glm_right.x, glm_right.y, glm_right.z};
    const glm::vec3 corrected_up = glm::normalize(glm::cross(glm_right, glm_forward));
    up = {corrected_up.x, corrected_up.y, corrected_up.z};

    // Atualiza a transformação
    m_transform->set_up(up);

    bgui::vec3 target;
    if (m_transform->is_using_target()) {
        target = m_transform->get_target();
    }
    else {
        target = position + forward;
    }

    // Agora, passa o vector 'up' atualizado para a viewMatrix
    viewMatrix = glm::lookAt(glm::vec3(position.x, position.y, position.z), glm::vec3(target.x, target.y, target.z), glm::vec3(up.x, up.y, up.z));
    return viewMatrix;
}
bgui::vec2i camera::viewport_size() const {
    return viewport_ptr ? *viewport_ptr : viewportFBO;
}

glm::mat4 camera::obtProjectionMatrix() {
    const auto viewp = viewport_size();

    if (viewp.x <= 0 || viewp.y <= 0)
        return glm::mat4(1.f);

    const float width = static_cast<float>(viewp.x);
    const float height = static_cast<float>(viewp.y);

    config.aspecto = width / height;

    if (config.flag_orth) {
        config.left = -config.scale * config.aspecto;
        config.right_limit = config.scale * config.aspecto;
        config.bottom = -config.scale;
        config.top = config.scale;

        projMatriz = glm::ortho(
            config.left,
            config.right_limit,
            config.bottom,
            config.top,
            config.corte_curto,
            config.corte_longo
        );
    } else {
        projMatriz = glm::perspective(
            glm::radians(config.fov),
            config.aspecto,
            config.corte_curto,
            config.corte_longo
        );
    }

    return projMatriz;
}

std::optional<glm::mat4> camera::view_projection() {
    const auto size = viewport_size();
    if (size.x <= 0 || size.y <= 0 || !std::isfinite(config.corte_curto) ||
        !std::isfinite(config.corte_longo) || config.corte_curto <= 0.f ||
        config.corte_longo <= config.corte_curto)
        return std::nullopt;
    if (config.flag_orth ? (!std::isfinite(config.scale) || config.scale <= 0.f)
                         : (!std::isfinite(config.fov) || config.fov <= 0.f || config.fov >= 180.f))
        return std::nullopt;
    const auto matrix = obtProjectionMatrix() * getViewMatrix();
    const auto determinant = glm::determinant(matrix);
    if (!std::isfinite(determinant) || determinant == 0.f)
        return std::nullopt;
    return matrix;
}

std::optional<bgui::vec3> camera::world_to_screen(const bgui::vec3& point) {
    const auto matrix = view_projection();
    if (!matrix) return std::nullopt;
    const auto clip = *matrix * glm::vec4(point.x, point.y, point.z, 1.f);
    if (clip.w <= 0.f || !std::isfinite(clip.w)) return std::nullopt;
    const auto ndc = glm::vec3(clip) / clip.w;
    const auto size = viewport_size();
    const bgui::vec3 screen{(ndc.x + 1.f) * 0.5f * size.x,
        (1.f - ndc.y) * 0.5f * size.y, (ndc.z + 1.f) * 0.5f};
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y) || !std::isfinite(screen.z))
        return std::nullopt;
    return screen;
}

std::optional<bgui::vec3> camera::screen_to_world(const bgui::vec3& point) {
    const auto matrix = view_projection();
    if (!matrix) return std::nullopt;
    const auto size = viewport_size();
    const auto world = glm::inverse(*matrix) * glm::vec4(
        2.f * (point.x / size.x) - 1.f, 1.f - 2.f * (point.y / size.y),
        2.f * point.z - 1.f, 1.f);
    if (world.w == 0.f || !std::isfinite(world.w)) return std::nullopt;
    const auto result = glm::vec3(world) / world.w;
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        return std::nullopt;
    return bgui::vec3{result.x, result.y, result.z};
}

ray camera::point_to_ray(const bgui::vec2& screenPoint) const
{
    bgui::vec3 directionMundo = telaParaMundo(screenPoint, 0.0f);

    ray ray {};
    ray.origem = position;
    ray.direction = normalized(directionMundo);

    return ray;
}

bgui::vec3 camera::telaParaMundo(const bgui::vec2 &screenPoint, float profundidade) const
{
    float ndcX = (2.0f * screenPoint.x) / viewportFBO.x - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPoint.y) / viewportFBO.y;
    bgui::vec4 clipCoords = {ndcX, ndcY, profundidade, 1.0f};

    const glm::vec4 eye = glm::inverse(projMatriz) * glm::vec4(clipCoords.x, clipCoords.y, clipCoords.z, clipCoords.w);
    bgui::vec4 eyeCoords = {eye.x, eye.y, eye.z, eye.w};
    eyeCoords = {eyeCoords.x, eyeCoords.y, -1.0f, 0.0f};

    const glm::vec4 world = glm::inverse(viewMatrix) * glm::vec4(eyeCoords.x, eyeCoords.y, eyeCoords.z, eyeCoords.w);
    return normalized({world.x, world.y, world.z});
}

bgui::vec2i camera::worldParaTela(const bgui::vec3 &worldPos)
{
    glm::vec4 clipSpacePos = projMatriz * viewMatrix * glm::vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);

    // Validação de w para evitar divisões inválidas
    if (clipSpacePos.w <= 0.0001f) {
        return {-1, -1}; // ou outro tratamento adequado
    }

    glm::vec3 ndcPos = glm::vec3(clipSpacePos) / clipSpacePos.w;

    int screenWidth = viewport_ptr->x;
    int screenHeight = viewport_ptr->y;

    bgui::vec2i screenPos;
    screenPos.x = static_cast<int>(std::round((ndcPos.x * 0.5f + 0.5f) * screenWidth));
    screenPos.y = static_cast<int>(std::round((1.0f - (ndcPos.y * 0.5f + 0.5f)) * screenHeight)); // Inverter Y
    return screenPos;
}

void camera::move(const bgui::vec3& pos)
{
    const auto registry = reg.lock();
    const auto owned_transform = registry ? registry->get<transform>(my_object) : nullptr;
    // Preserve explicit transforms on standalone cameras, but discard expired ECS caches.
    const std::weak_ptr<ecs> empty_registry;
    if (reg.owner_before(empty_registry) || empty_registry.owner_before(reg))
        m_transform = owned_transform.get();
    if (!m_transform)
        return;

    // Atualiza a posição com base na entrada
    m_transform->move(forward * pos.z);  // Move para frente/trás
    m_transform->move(right * pos.x);  // Move para os lados
    m_transform->move(up * pos.y);     // Move para up/baixo
}

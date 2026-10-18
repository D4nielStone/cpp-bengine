#include "glad.h"
#include "debugging/debug.hpp"
#include "components/camera.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"

using namespace COMMONS_NS;

/**
 * @brief Ativa a escrita do framebuffer da câmera
 */

bgui::vec3 normalized(const bgui::vec3& v) {
    float length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0) return {0.f, 0.f, 0.f};
    return {v.x / length, v.y / length, v.z / length};
}

void camera::drawFB() {
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

        glClearColor(ceu.r, ceu.g, ceu.b, ceu.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(ceu.r, ceu.g, ceu.b, ceu.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


camera::~camera()
{
    debugging::emit(debug, "camera", "descarregando");

    if(m_skybox)delete m_skybox;
    deleteFB();
}

camera::camera(const bool orth)
    : flag_orth(orth) {
}

bool camera::analyze(const rapidjson::Value& value)
{
    m_skybox = new skybox();

    if(value.HasMember("fov"))
        fov = value["fov"].GetFloat();
    if(value.HasMember("zfar"))
        corte_longo = value["zfar"].GetFloat();
	if (value.HasMember("scale"))
		scale = value["scale"].GetFloat();
	if (value.HasMember("ortho"))
		flag_orth = value["ortho"].GetBool();
	if (value.HasMember("ceu"))
	{
		auto _ceu = value["ceu"].GetArray();
		ceu =
		{
			_ceu[0].GetFloat() / 255,
			_ceu[1].GetFloat() / 255,
			_ceu[2].GetFloat() / 255,
			_ceu[3].GetFloat() / 255,
		};
	}
	if (value.HasMember("skybox") && !value["skybox"].GetBool()) return true;

    m_use_skybox = true;

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
    // fov
    value.AddMember("fov", fov, allocator);

    // zfar
    value.AddMember("zfar", corte_longo, allocator);

    // scale
    value.AddMember("scale", scale, allocator);

    // ortho flag
    value.AddMember("ortho", flag_orth, allocator);

    // ceu (vector RGBA)
    rapidjson::Value cor_ceu(rapidjson::kArrayType);
    cor_ceu.PushBack(static_cast<int>(ceu.r * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(ceu.g * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(ceu.b * 255), allocator);
    cor_ceu.PushBack(static_cast<int>(ceu.a * 255), allocator);
    value.AddMember("ceu", cor_ceu, allocator);

    // skybox (flag)
    value.AddMember("skybox", m_use_skybox, allocator);

    return true;
}

void camera::deleteFB()
{
    if (!flag_fb) return;
    flag_fb = false;

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    glDeleteRenderbuffers(1, &rbo);
}

glm::mat4 camera::getViewMatrix() {
    if (!m_transform && reg)
        m_transform = reg->get<transform>(my_object).get();
    if (!m_transform)
        return viewMatrix;

    position = m_transform->get_position();

    // Recalculate reference vectors
    bgui::vec3 frente = {
        cos(glm::radians(m_transform->get_rotation().y)) * cos(glm::radians(m_transform->get_rotation().x)),    // x
        sin(glm::radians(m_transform->get_rotation().x)),                                                     // y
        sin(glm::radians(m_transform->get_rotation().y)) * cos(glm::radians(m_transform->get_rotation().x))     // z
    };
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
glm::mat4 camera::obtProjectionMatrix() {
    const bgui::vec2i viewp = viewport_ptr ? *viewport_ptr : viewportFBO;

    if (viewp.x <= 0 || viewp.y <= 0)
        return glm::mat4(1.f);

    const float width = static_cast<float>(viewp.x);
    const float height = static_cast<float>(viewp.y);

    aspecto = width / height;

    if (flag_orth) {
        left = -scale * aspecto;
        right_limit = scale * aspecto;
        bottom = -scale;
        top = scale;

        projMatriz = glm::ortho(
            left,
            right_limit,
            bottom,
            top,
            corte_curto,
            corte_longo
        );
    } else {
        projMatriz = glm::perspective(
            glm::radians(fov),
            aspecto,
            corte_curto,
            corte_longo
        );
    }

    return projMatriz;
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
    if (!m_transform && reg)
        m_transform = reg->get<transform>(my_object).get();
    if (!m_transform)
        return;

    // Atualiza a posição com base na entrada
    m_transform->move(forward * pos.z);  // Move para frente/trás
    m_transform->move(right * pos.x);  // Move para os lados
    m_transform->move(up * pos.y);     // Move para up/baixo
}
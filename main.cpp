// Slime - mod de Geode para Geometry Dash (Android 2.2081 / Geode 5.11.2).
//
// 1) Rebote de slime: cualquier objeto que pongas en el grupo configurado (777 por
//    defecto) devuelve al jugador hacia arriba con la velocidad con la que cayo.
//    No hace falta tocar el editor: se asigna el grupo al bloque y listo.
// 2) Estilo slime (opcional): un shader que pinta los bloques y la decoracion
//    como gelatina verde. Funciona por su cuenta, tambien junto con DLSS5 Geode.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "SlimeLogic.hpp"

using namespace geode::prelude;

namespace {

// ----------------------------------------------------------------------------
// Ajustes
// ----------------------------------------------------------------------------

struct Cfg {
    bool bounce = true;
    int group = 777;
    slime::Params params;
    bool style = false;
    float amount = 0.85f;
};

Cfg& cfg() {
    static Cfg c;
    return c;
}

unsigned& tickCounter() {
    static unsigned n = 0;
    return n;
}

void refreshCfg() {
    auto* mod = Mod::get();
    auto& c = cfg();
    c.bounce = mod->getSettingValue<bool>("bounce-enabled");
    c.group = static_cast<int>(mod->getSettingValue<int64_t>("slime-group"));
    c.params.power = static_cast<double>(mod->getSettingValue<int64_t>("bounce-power")) / 100.0;
    c.params.minImpact = mod->getSettingValue<double>("bounce-min-fall");
    c.params.minOut = mod->getSettingValue<double>("bounce-min");
    c.params.maxOut = mod->getSettingValue<double>("bounce-max");
    c.style = mod->getSettingValue<bool>("slime-style");
    c.amount = static_cast<float>(mod->getSettingValue<int64_t>("style-amount")) / 100.f;
}

bool isSlime(GameObject* obj) {
    if (!obj || !obj->m_groups) return false;
    return slime::inGroup(obj->m_groups->data(), obj->m_groupCount, cfg().group);
}

bool isMainPlayLayer(GJBaseGameLayer* layer) {
    auto* pl = PlayLayer::get();
    return pl != nullptr && static_cast<GJBaseGameLayer*>(pl) == layer;
}

// ----------------------------------------------------------------------------
// Estilo slime (shader)
// ----------------------------------------------------------------------------

const char* kVert = R"GLSL(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)GLSL";

const char* kFrag = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform float u_time;
uniform float u_amount;
uniform vec2 u_texel;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec4 base = v_fragmentColor * texture2D(CC_Texture0, v_texCoord);
    if (base.a < 0.003) {
        gl_FragColor = base;
        return;
    }
    vec3 col = base.rgb / base.a;

    // relieve a partir de la luminosidad de los texeles vecinos
    float lr = luma(texture2D(CC_Texture0, v_texCoord + vec2(u_texel.x, 0.0)).rgb);
    float ll = luma(texture2D(CC_Texture0, v_texCoord - vec2(u_texel.x, 0.0)).rgb);
    float lu = luma(texture2D(CC_Texture0, v_texCoord + vec2(0.0, u_texel.y)).rgb);
    float ld = luma(texture2D(CC_Texture0, v_texCoord - vec2(0.0, u_texel.y)).rgb);
    vec3 n = normalize(vec3((ll - lr) * 3.0, (ld - lu) * 3.0, 1.0));

    vec3 lightDir = normalize(vec3(-0.4, 0.6, 0.7));
    float diff = clamp(dot(n, lightDir), 0.0, 1.0);
    float spec = pow(clamp(dot(n, normalize(lightDir + vec3(0.0, 0.0, 1.0))), 0.0, 1.0), 24.0);

    // gelatina: de verde profundo a verde lima segun la luminosidad original
    float body = clamp(luma(col), 0.0, 1.0);
    vec3 deep = vec3(0.03, 0.30, 0.05);
    vec3 mid = vec3(0.20, 0.80, 0.16);
    vec3 glow = vec3(0.70, 1.00, 0.40);
    vec3 slimeCol = mix(deep, mid, smoothstep(0.05, 0.6, body));
    slimeCol = mix(slimeCol, glow, smoothstep(0.55, 1.0, body) * 0.8);
    slimeCol *= 0.75 + 0.35 * diff;

    // temblor suave de la superficie y brillo humedo
    float wob = sin(u_time * 2.4 + v_texCoord.x * 420.0) * sin(u_time * 1.7 + v_texCoord.y * 380.0);
    slimeCol += 0.035 * wob;
    slimeCol += vec3(0.85, 1.0, 0.8) * spec * 0.6;

    vec3 outCol = mix(col, slimeCol, clamp(u_amount, 0.0, 1.0));
    gl_FragColor = vec4(outCol * base.a, base.a);
}
)GLSL";

struct Style {
    CCGLProgram* program = nullptr;
    GLint uTime = -1;
    GLint uAmount = -1;
    GLint uTexel = -1;
    float time = 0.f;
    float since = 0.f;
    bool applied = false;
};

Style& style() {
    static Style s;
    return s;
}

// Capas de bloques y decoracion (sin las de brillo / mezcla aditiva).
template <class F>
void forEachBlockLayer(GJBaseGameLayer* l, F&& f) {
    CCSpriteBatchNode* nodes[] = {
        l->m_gameLayerT4, l->m_game2LayerT4, l->m_gameLayerT3, l->m_game2LayerT3,
        l->m_gameLayerT2, l->m_game2LayerT2, l->m_gameLayerT1, l->m_game2LayerT1,
        l->m_gameLayerB1, l->m_game2LayerB1, l->m_gameLayerB2, l->m_game2LayerB2,
        l->m_gameLayerB3, l->m_game2LayerB3, l->m_gameLayerB4, l->m_game2LayerB4,
    };
    for (auto* n : nodes) {
        if (n) f(n);
    }
}

void uploadUniforms(bool withTime) {
    auto& s = style();
    if (!s.program) return;
    s.program->use();
    if (withTime) s.program->setUniformLocationWith1f(s.uTime, s.time);
    s.program->setUniformLocationWith1f(s.uAmount, cfg().amount);
    s.program->setUniformLocationWith2f(s.uTexel, 1.f / 2048.f, 1.f / 2048.f);
}

bool buildProgram() {
    auto& s = style();
    auto* p = new CCGLProgram();
    if (!p->initWithVertexShaderByteArray(kVert, kFrag)) {
        p->release();
        log::warn("Slime: no se pudo compilar el shader; el estilo slime queda apagado");
        return false;
    }
    p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    if (!p->link()) {
        p->release();
        log::warn("Slime: no se pudo enlazar el shader; el estilo slime queda apagado");
        return false;
    }
    p->updateUniforms();
    if (s.program) s.program->release();
    s.program = p;
    s.uTime = p->getUniformLocationForName("u_time");
    s.uAmount = p->getUniformLocationForName("u_amount");
    s.uTexel = p->getUniformLocationForName("u_texel");
    uploadUniforms(true);
    return true;
}

void applyStyle(GJBaseGameLayer* layer) {
    auto& s = style();
    if (!s.program && !buildProgram()) {
        s.applied = false;
        return;
    }
    forEachBlockLayer(layer, [&](CCSpriteBatchNode* n) { n->setShaderProgram(s.program); });
    s.applied = true;
}

// Si Android perdio el contexto de OpenGL (volviste de otra app) el programa deja de existir:
// se vuelve a crear y a poner en las capas.
void keepStyleAlive(GJBaseGameLayer* layer) {
    auto& s = style();
    if (!s.applied || !s.program) return;
    if (glIsProgram(s.program->getProgram())) {
        uploadUniforms(false);
        return;
    }
    if (buildProgram()) {
        forEachBlockLayer(layer, [&](CCSpriteBatchNode* n) { n->setShaderProgram(s.program); });
    }
}

} // namespace

// ----------------------------------------------------------------------------
// Rebote
// ----------------------------------------------------------------------------

class $modify(SlimePlayer, PlayerObject) {
    struct Fields {
        double afterUpdateVy = 0.0; // velocidad al terminar el update (justo antes de las colisiones)
        double impactVy = 0.0;
        bool pending = false;
    };

    void slimeApply() {
        m_fields->pending = false;
        auto out = slime::bounceVelocity(
            m_fields->impactVy, m_yVelocity, m_isUpsideDown, cfg().params
        );
        if (!out) return;
        m_yVelocity = *out;
        m_isOnGround = false;
    }

    void update(float dt) {
        if (m_fields->pending) this->slimeApply(); // por si postCollision no llego a correr
        PlayerObject::update(dt);
        m_fields->afterUpdateVy = m_yVelocity;
    }

    void hitGround(GameObject* object, bool notFlipped) {
        double entryVy = m_yVelocity;
        PlayerObject::hitGround(object, notFlipped);
        if (!cfg().bounce || m_isDead || m_isDart) return;
        if (!isSlime(object)) return;
        m_fields->impactVy = slime::pickImpact(entryVy, m_fields->afterUpdateVy, m_isUpsideDown);
        m_fields->pending = true;
    }

    void postCollision(float dt, bool betweenSteps) {
        PlayerObject::postCollision(dt, betweenSteps);
        if (m_fields->pending) this->slimeApply();
    }
};

// ----------------------------------------------------------------------------
// Ciclo del nivel (estilo slime y lectura de ajustes)
// ----------------------------------------------------------------------------

class $modify(SlimePlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        refreshCfg();
        auto& s = style();
        s.applied = false;
        s.time = 0.f;
        s.since = 0.f;
        if (cfg().style) applyStyle(this);
    }
};

class $modify(SlimeBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (!isMainPlayLayer(this)) return;
        if ((tickCounter()++ % 30u) == 0u) {
            refreshCfg();
            keepStyleAlive(this);
        }
        auto& s = style();
        if (!s.applied) return;
        s.time += dt;
        s.since += dt;
        if (s.since >= 0.02f) {
            s.since = 0.f;
            uploadUniforms(true);
        }
    }
};

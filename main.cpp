// Slime - mod de Geode para Geometry Dash (Android 2.2081 / Geode 5.11.2).
//
// 1) Rebote de slime: cualquier objeto que pongas en el grupo configurado (777 por
//    defecto) devuelve al jugador hacia arriba con la velocidad con la que cayo.
//    No hace falta tocar el editor: se asigna el grupo al bloque y listo.
// 2) Estilo slime (opcional, solo visual): una textura de slime realista (brillo humedo,
//    relieve viscoso, borde redondeado y burbujas) que CONSERVA el color original de cada
//    bloque. No toca la fisica. Funciona por su cuenta, tambien junto con DLSS5 Geode.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <vector>

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
    float gloss = 0.70f;
    float bubbles = 0.40f;
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
    c.gloss = static_cast<float>(mod->getSettingValue<int64_t>("style-gloss")) / 100.f;
    c.bubbles = static_cast<float>(mod->getSettingValue<int64_t>("style-bubbles")) / 100.f;
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
// Estilo slime (shader). Solo cambia como se dibujan los bloques y la decoracion:
// el color de cada objeto se conserva y se le agrega material de slime encima.
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
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform float u_time;
uniform float u_amount;
uniform float u_gloss;
uniform float u_bubbles;
uniform vec2 u_texel;
uniform float u_pscale;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// ruido de valor que se repite cada "period" celdas (sin costuras)
float vnoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(mod(i, period));
    float b = hash12(mod(i + vec2(1.0, 0.0), period));
    float c = hash12(mod(i + vec2(0.0, 1.0), period));
    float d = hash12(mod(i + vec2(1.0, 1.0), period));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// altura de la superficie viscosa: dos octavas de ondulaciones grandes que fluyen despacio
float gooHeight(vec2 px, float t)
{
    vec2 p = mod(px + vec2(t * 4.0, t * 2.8), 256.0);
    return vnoise(p / 64.0, 4.0) * 0.65 + vnoise(p / 32.0 + 3.7, 8.0) * 0.35;
}

// burbujas: x = brillo (aro + destello), y = oscurecimiento del interior
vec2 bubbles(vec2 p, float cellSize, float period, float thresh, float radMin, float radRange, float seed)
{
    vec2 bp = p / cellSize;
    vec2 cell = floor(bp);
    vec2 f = fract(bp) - 0.5;
    cell = mod(cell, period);
    float present = step(thresh, hash12(cell + seed));
    vec2 jitter = (vec2(hash12(cell + seed + 4.1), hash12(cell + seed + 8.3)) - 0.5) * 0.30;
    float rad = radMin + radRange * hash12(cell + seed + 2.9);
    vec2 d = f - jitter;
    float dist = length(d);
    float ring = smoothstep(rad - 0.10, rad - 0.04, dist) * (1.0 - smoothstep(rad - 0.04, rad, dist));
    float body = 1.0 - smoothstep(rad * 0.25, rad, dist);
    float glint = 1.0 - smoothstep(0.0, rad * 0.34, length(d - vec2(-0.38, -0.38) * rad));
    return present * vec2(ring * 0.42 + glint * 0.95, body * 0.14);
}

void main()
{
    vec4 base = v_fragmentColor * texture2D(CC_Texture0, v_texCoord);
    if (base.a < 0.003) {
        gl_FragColor = base;
        return;
    }
    vec3 col = clamp(base.rgb / base.a, 0.0, 1.0);

    // relieve fino del propio bloque (bordes, lineas) y borde redondeado tipo gota
    vec2 o1 = u_texel * 1.2;
    vec2 o2 = u_texel * 7.0;
    vec4 tr = texture2D(CC_Texture0, v_texCoord + vec2(o1.x, 0.0));
    vec4 tl = texture2D(CC_Texture0, v_texCoord - vec2(o1.x, 0.0));
    vec4 tu = texture2D(CC_Texture0, v_texCoord - vec2(0.0, o1.y));
    vec4 td = texture2D(CC_Texture0, v_texCoord + vec2(0.0, o1.y));
    float ar = texture2D(CC_Texture0, v_texCoord + vec2(o2.x, 0.0)).a;
    float al = texture2D(CC_Texture0, v_texCoord - vec2(o2.x, 0.0)).a;
    float au = texture2D(CC_Texture0, v_texCoord - vec2(0.0, o2.y)).a;
    float ad = texture2D(CC_Texture0, v_texCoord + vec2(0.0, o2.y)).a;

    // superficie viscosa procedural
    vec2 px = v_texCoord * u_pscale;
    float h0 = gooHeight(px, u_time);
    float hx = gooHeight(px + vec2(2.0, 0.0), u_time) - h0;
    float hy = h0 - gooHeight(px + vec2(0.0, 2.0), u_time);

    vec2 slope = vec2(hx, hy) * 9.0
               + vec2(luma(tr.rgb) - luma(tl.rgb), luma(tu.rgb) - luma(td.rgb)) * 0.8
               + vec2(ar - al, au - ad) * 1.5;
    vec3 n = normalize(vec3(-slope, 1.0));

    vec3 L = normalize(vec3(-0.45, 0.65, 0.62));
    float diff = clamp((dot(n, L) + 0.4) / 1.4, 0.0, 1.0);
    float fres = pow(1.0 - clamp(n.z, 0.0, 1.0), 3.0);
    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float nh = clamp(dot(n, H), 0.0, 1.0);
    float spec = pow(nh, 70.0) + pow(nh, 14.0) * 0.28;
    float wet = 0.7 + 0.3 * vnoise(mod(px + 11.0, 256.0) / 32.0, 8.0);

    // gel con el color original: mas oscuro donde es mas grueso, mas claro en los bordes finos
    float lum = luma(col);
    vec3 sat = mix(vec3(lum), col, 1.08);
    vec3 inner = sat * (0.50 + 0.62 * diff) * (0.82 + 0.34 * (1.0 - h0));
    inner += col * col * 0.16 * (1.0 - diff);
    vec3 rim = mix(col, vec3(1.0), 0.22) * fres * 0.85;
    vec3 env = mix(vec3(0.62, 0.70, 0.80), vec3(1.0), n.y * 0.5 + 0.5);
    vec3 refl = env * (0.04 + 0.42 * fres);
    vec3 outc = inner + rim + refl + vec3(spec * wet * u_gloss);

    // burbujas dentro del gel (grandes y chicas)
    vec2 pb = mod(px + vec2(3.0, 7.0), 256.0);
    vec2 b1 = bubbles(pb, 32.0, 8.0, 0.78, 0.12, 0.12, 1.7);
    vec2 b2 = bubbles(pb + 19.0, 16.0, 16.0, 0.88, 0.10, 0.10, 5.3) * 0.6;
    // sin burbujas sobre los contornos oscuros ni pegadas al borde de la figura
    float inside = min(min(ar, al), min(au, ad)) * smoothstep(0.22, 0.5, lum);
    outc *= 1.0 - (b1.y + b2.y) * u_bubbles * inside;
    outc += vec3((b1.x + b2.x) * u_bubbles * inside);

    outc = min(outc, vec3(1.0));
    outc = mix(col, outc, clamp(u_amount, 0.0, 1.0));
    gl_FragColor = vec4(outc * base.a, base.a);
}
)GLSL";

// Un programa por tamano de textura (hace falta para leer los texeles vecinos).
struct Prog {
    CCGLProgram* program = nullptr;
    GLint uTime = -1;
    GLint uAmount = -1;
    GLint uGloss = -1;
    GLint uBubbles = -1;
    GLint uTexel = -1;
    GLint uPscale = -1;
    int w = 0;
    int h = 0;
};

struct Style {
    std::vector<Prog> progs;
    float time = 0.f;
    float since = 0.f;
    bool applied = false;
    bool failed = false;
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

void uploadUniforms(Prog& p, bool withTime) {
    auto* g = p.program;
    g->use();
    if (withTime) g->setUniformLocationWith1f(p.uTime, style().time);
    g->setUniformLocationWith1f(p.uAmount, cfg().amount);
    g->setUniformLocationWith1f(p.uGloss, cfg().gloss);
    g->setUniformLocationWith1f(p.uBubbles, cfg().bubbles);
    g->setUniformLocationWith2f(p.uTexel, 1.f / static_cast<float>(p.w), 1.f / static_cast<float>(p.h));
    g->setUniformLocationWith1f(p.uPscale, 4096.f); // las hojas de GD comparten el mismo mapa UV en todas las calidades
}

CCGLProgram* buildProgram() {
    auto* p = new CCGLProgram();
    if (!p->initWithVertexShaderByteArray(kVert, kFrag)) {
        p->release();
        log::warn("Slime: no se pudo compilar el shader; el estilo slime queda apagado");
        return nullptr;
    }
    p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    if (!p->link()) {
        p->release();
        log::warn("Slime: no se pudo enlazar el shader; el estilo slime queda apagado");
        return nullptr;
    }
    p->updateUniforms();
    return p;
}

Prog* programFor(int w, int h) {
    auto& s = style();
    for (auto& p : s.progs) {
        if (p.w == w && p.h == h) return &p;
    }
    if (s.failed) return nullptr;
    auto* program = buildProgram();
    if (!program) {
        s.failed = true;
        return nullptr;
    }
    Prog p;
    p.program = program;
    p.w = w;
    p.h = h;
    p.uTime = program->getUniformLocationForName("u_time");
    p.uAmount = program->getUniformLocationForName("u_amount");
    p.uGloss = program->getUniformLocationForName("u_gloss");
    p.uBubbles = program->getUniformLocationForName("u_bubbles");
    p.uTexel = program->getUniformLocationForName("u_texel");
    p.uPscale = program->getUniformLocationForName("u_pscale");
    s.progs.push_back(p);
    uploadUniforms(s.progs.back(), true);
    return &s.progs.back();
}

void clearPrograms() {
    auto& s = style();
    for (auto& p : s.progs) {
        if (p.program) p.program->release();
    }
    s.progs.clear();
}

void applyStyle(GJBaseGameLayer* layer) {
    auto& s = style();
    bool any = false;
    forEachBlockLayer(layer, [&](CCSpriteBatchNode* n) {
        auto* tex = n->getTexture();
        int w = tex ? static_cast<int>(tex->getPixelsWide()) : 2048;
        int h = tex ? static_cast<int>(tex->getPixelsHigh()) : 2048;
        if (w <= 0 || h <= 0) w = h = 2048;
        if (auto* pr = programFor(w, h)) {
            n->setShaderProgram(pr->program);
            any = true;
        }
    });
    s.applied = any;
}

// Si Android perdio el contexto de OpenGL (volviste de otra app) los programas dejan de existir:
// se vuelven a crear y a poner en las capas.
void keepStyleAlive(GJBaseGameLayer* layer) {
    auto& s = style();
    if (!s.applied || s.progs.empty()) return;
    bool ok = true;
    for (auto& p : s.progs) {
        if (!glIsProgram(p.program->getProgram())) ok = false;
    }
    if (ok) {
        for (auto& p : s.progs) uploadUniforms(p, false);
        return;
    }
    clearPrograms();
    applyStyle(layer);
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
        s.failed = false;
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
            for (auto& p : s.progs) uploadUniforms(p, true);
        }
    }
};

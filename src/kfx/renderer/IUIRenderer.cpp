/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.cpp
 *     Software draw path for the shared UI submission API, plus the IR
 *     bind machinery a deferring backend (GL) needs.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/renderer/IUIRenderer.h"
#include "kfx/renderer/ir/UICommands.h"
#include "kfx/renderer/RendererManager.h"
#include "bflib_vidraw.h"   // the raster primitives
#include "bflib_sprite.h"   // TbSprite, num_sprites, get_sprite
#include "bflib_video.h"    // Lb_SPRITE_* draw flags
#include "gui_draw.h"       // draw_slab64k_background_immediate
#include <algorithm>        // std::min, std::max (SubmitMinimap)
#include <functional>       // std::less (ForgetSprites)
#include "post_inc.h"

/******************************************************************************/

namespace {

/** Applies a submitted draw state for the duration of one draw. */
class ScopedDrawState {
public:
    explicit ScopedDrawState(TbDrawFlagsMask flags)
        : m_flags(RendererGetDrawFlags())
    {
        RendererSetDrawFlags((unsigned short)flags);
    }
    ~ScopedDrawState() { RendererSetDrawFlags(m_flags); }
private:
    unsigned short m_flags;
};

} // namespace

/******************************************************************************/
// Sprite handle registry

void IUIRenderer::RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr)
{
    std::lock_guard<std::mutex> guard(m_handle_mutex);
    m_handle_to_sprite[h] = spr;
    m_sprite_to_handle[spr] = h;
}

SpriteHandle IUIRenderer::ResolveSprite(const struct TbSprite* spr)
{
    if (!spr) return kInvalidSpriteHandle;
    std::lock_guard<std::mutex> guard(m_handle_mutex);
    auto it = m_sprite_to_handle.find(spr);
    if (it != m_sprite_to_handle.end())
        return it->second;
    SpriteHandle h = m_next_handle++;
    m_handle_to_sprite[h] = spr;
    m_sprite_to_handle[spr] = h;
    return h;
}

int32_t IUIRenderer::ForgetSprites(const struct TbSprite* first, size_t count)
{
    if (first == nullptr || count == 0)
        return 0;
    const std::less<const struct TbSprite*> before;
    const struct TbSprite* const end = first + count;
    std::lock_guard<std::mutex> guard(m_handle_mutex);
    int32_t dropped = 0;
    for (auto it = m_sprite_to_handle.begin(); it != m_sprite_to_handle.end(); )
    {
        if (!before(it->first, first) && before(it->first, end))
        {
            m_handle_to_sprite.erase(it->second);
            it = m_sprite_to_handle.erase(it);
            ++dropped;
        }
        else
        {
            ++it;
        }
    }
    return dropped;
}

void IUIRenderer::RegisterSpriteSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    const int32_t n = (int32_t)num_sprites(sheet);
    for (int32_t i = 0; i < n; ++i)
        ResolveSprite(get_sprite(sheet, i));
}

/******************************************************************************/
// Submission

TbResult IUIRenderer::SubmitRawSprite(int32_t x, int32_t y, const struct TbSprite* spr,
                                      KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    if (m_ui_write_cmds) {
        IRUISpriteCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y;
        cmd.sprite = ResolveSprite(spr);
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites.Append(cmd);
        return Lb_SUCCESS;
    }
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawImmediate(x, y, spr);
}

TbResult IUIRenderer::SubmitRawSpriteOneColour(int32_t x, int32_t y, const struct TbSprite* spr,
                                               unsigned char colour, KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    if (m_ui_write_cmds) {
        IRUISpriteOneColourCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y;
        cmd.sprite = ResolveSprite(spr);
        cmd.colour = colour;
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_one_colour.Append(cmd);
        return Lb_SUCCESS;
    }
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawOneColourImmediate(x, y, spr, colour);
}

TbResult IUIRenderer::SubmitRawSpriteScaled(int32_t x, int32_t y, const struct TbSprite* spr,
                                            int32_t w, int32_t h, KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    if (m_ui_write_cmds) {
        IRUISpriteScaledCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.sprite = ResolveSprite(spr);
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_scaled.Append(cmd);
        return Lb_SUCCESS;
    }
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledImmediate(x, y, spr, w, h);
}

TbResult IUIRenderer::SubmitRawSpriteScaledOneColour(int32_t x, int32_t y, const struct TbSprite* spr,
                                                     int32_t w, int32_t h, unsigned char colour,
                                                     KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    if (m_ui_write_cmds) {
        IRUISpriteScaledOneColourCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.sprite = ResolveSprite(spr);
        cmd.colour = colour;
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_scaled_one_colour.Append(cmd);
        return Lb_SUCCESS;
    }
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledOneColourImmediate(x, y, spr, w, h, colour);
}

int IUIRenderer::SubmitRawSpriteScaledRemap(int32_t x, int32_t y, const struct TbSprite* spr,
                                            int32_t w, int32_t h, const unsigned char* cmap,
                                            KfxDrawState state)
{
    if (!spr || !cmap) return Lb_FAIL;
    if (m_ui_write_cmds) {
        IRUISpriteScaledRemapCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.sprite = ResolveSprite(spr);
        cmd.cmap = cmap;
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_scaled_remap.Append(cmd);
        return Lb_SUCCESS;
    }
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledRemapImmediate(x, y, spr, w, h, cmap);
}

void IUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h,
                                 uint8_t colour_idx, KfxDrawState state)
{
    if (w <= 0 || h <= 0) return;
    if (m_ui_write_cmds) {
        IRUISolidBoxCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyGraphicsWindow(x, y, cmd.clip);
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.colour = colour_idx;
        cmd.draw_flags = state.flags;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->solid_boxes.Append(cmd);
        return;
    }
    // LbDrawBox reads the outline flag itself, so the whole state just goes ambient.
    ScopedDrawState guard(state.flags);
    LbDrawBoxImmediate(x, y, (unsigned long)w, (unsigned long)h, colour_idx);
}

void IUIRenderer::SubmitSlabBackground(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (m_ui_write_cmds) {
        IRUISlabBackgroundCmd cmd;
        cmd.layer = ComputeCurrentLayer();
        ApplyWindowOffset(cmd.layer, x, y);
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.ndc_z = ComputeCurrentNdcZ();
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->slab_backgrounds.Append(cmd);
        return;
    }
    draw_slab64k_background_immediate(x, y, w, h);
}

uint16_t* IUIRenderer::AcquireMinimapBuffer(int size)
{
    if (size <= 0) return nullptr;
    const size_t needed = (size_t)size * (size_t)size;
    m_minimap_cpu_buf.assign(needed, MinimapPixelTransparent);
    m_minimap_cpu_size = size;
    return m_minimap_cpu_buf.data();
}

void IUIRenderer::SubmitMinimap(int screen_x, int screen_y, int size,
                                const uint8_t* colours, int colour_count)
{
    // CPU default: resolve each pixel against the framebuffer, leaving
    // transparent pixels untouched so panel art outside the circle survives.
    if (size <= 0 || m_minimap_cpu_size != size || lbDisplay.WScreen == NULL || colours == nullptr || colour_count <= 0)
        return;
    const int32_t stride = RendererScreenWidth();
    const int x0 = std::max(0, -screen_x);
    const int y0 = std::max(0, -screen_y);
    const int x1 = std::min(size, stride - screen_x);
    const int y1 = std::min(size, (int32_t)RendererScreenHeight() - screen_y);
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0; y < y1; y++) {
        TbPixel* out = &lbDisplay.WScreen[(screen_y + y) * stride + screen_x + x0];
        const uint16_t* src = &m_minimap_cpu_buf[y * size + x0];
        for (int x = x0; x < x1; x++, src++, out++) {
            if (*src < MinimapPixelKind) {
                *out = *src;
            } else if (*src != MinimapPixelTransparent && *src - MinimapPixelKind < colour_count) {
                *out = colours[(*src - MinimapPixelKind) * 256 + *out];
            }
        }
    }
}

void IUIRenderer::BeginZoomBoxOverlay(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LbScreenSetGraphicsWindow(x, y, w, h);
    m_zoom_box_x = x;
    m_zoom_box_y = y;
    m_zoom_box_active = true;
    SetTopOverlay();
}

void IUIRenderer::EndZoomBoxOverlay(int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)x; (void)y; (void)w; (void)h;
    m_zoom_box_active = false;
    ClearTopOverlay();
    LbScreenSetGraphicsWindow(0, 0, RendererScreenWidth(), RendererScreenHeight());
}

/******************************************************************************/
// Frame wiring

void IUIRenderer::SetUICommandBuffers(UICommandBuffers* cmds)
{
    m_ui_write_cmds = cmds;
}

void IUIRenderer::SetGameViewport(int32_t x, int32_t y, int32_t w, int32_t h)
{
    m_game_vp_x = x; m_game_vp_y = y; m_game_vp_w = w; m_game_vp_h = h;
    m_game_vp_set = true;
    if (m_ui_write_cmds) m_ui_write_cmds->game_vp = { x, y, w, h, true };
}

/******************************************************************************/

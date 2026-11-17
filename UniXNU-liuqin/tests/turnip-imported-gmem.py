#!/usr/bin/env python3
"""Execute actual GMEM load/store/layout decisions for tightly owned host images."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--mesa', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--source-file', type=Path)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=True)
source = (a.source_file or a.mesa/'src/freedreno/vulkan/tu_clear_blit.cc').read_text()

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end+1]
    raise RuntimeError('Unclosed function: ' + signature)

parts = []
if '\ntu_attachment_unpadded(' in source:
    parts.append(function('static bool\ntu_attachment_unpadded('))
parts += [function(signature) for signature in (
    'template <chip CHIP>\nvoid\ntu_load_gmem_attachment(',
    'static bool\ntu_attachment_store_unaligned(',
    'static bool\ntu_attachment_store_mismatched_mutability(',
    'void\ntu_choose_gmem_layout(',
)]

fixture = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
enum chip {A7XX=7};
using VkExternalMemoryHandleTypeFlags=uint32_t;
enum {TILE6_LINEAR=0,TILE6_3=3,VK_IMAGE_TILING_OPTIMAL=0,VK_IMAGE_TILING_LINEAR=1,
      VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT=2,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT=128,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT=256,
      VK_FORMAT_D32_SFLOAT_S8_UINT=130,VK_FORMAT_S8_UINT=127,
      TU_GMEM_LAYOUT_FULL=0,TU_GMEM_LAYOUT_AVOID_CCU=1,BLIT_EVENT_LOAD=0};
static constexpr uint32_t VK_ATTACHMENT_UNUSED=~0u;
static bool debug_3d,debug_unaligned;
#define TU_DEBUG(x) debug_##x
#define debug_3D_LOAD debug_3d
#define debug_UNALIGNED_STORE debug_unaligned
struct VkOffset2D {int32_t x=0,y=0;};
struct VkExtent2D {uint32_t width=0,height=0;};
struct VkRect2D {VkOffset2D offset;VkExtent2D extent;};
struct fd_dev_info {unsigned gmem_align_w=16,gmem_align_h=4;};
struct tu_physical_device {fd_dev_info *info;};
struct tu_cmd_buffer;
struct Autotune {unsigned get_tile_size_divisor(tu_cmd_buffer *){return 1;}};
struct tu_device {tu_physical_device *physical_device;Autotune *autotune;};
struct tu_image {
    struct {unsigned tile_mode=TILE6_LINEAR;} layout[1];
    struct {unsigned tiling=VK_IMAGE_TILING_LINEAR,external_handle_types=0;} vk;
};
struct tu_image_view {
    tu_image *image;
    struct {unsigned width=0,height=0;bool need_y2_align=false,is_mutable=false;} view;
};
struct tu_render_pass_attachment {
    bool load=true,load_stencil=false,store=true,store_stencil=false,will_be_resolved=false,cond_load_allowed=true;
    unsigned format=0;
};
struct Reference {uint32_t attachment=0;};
struct tu_subpass {
    bool custom_resolve=false;unsigned resolve_count=0,color_count=0;
    Reference *resolve_attachments=nullptr,*color_attachments=nullptr,depth_stencil_attachment;
};
struct Pass {
    bool has_fdm=false;unsigned num_views=1,attachment_count=1,subpass_count=0;
    tu_render_pass_attachment *attachments;tu_subpass *subpasses=nullptr;
};
struct tu_framebuffer {};
struct tu_cs {};
struct tu_resolve_group {};
struct tu_cmd_buffer {
    tu_device *device;int rp_trace=0;
    struct {Pass *pass;tu_image_view **attachments;VkRect2D render_areas[2];
        bool per_layer_render_area=false,fdm_subsampled=false;
        unsigned gmem_layout=0,gmem_layout_divisor=0;
        tu_framebuffer *framebuffer=nullptr;const void *tiling=nullptr;} state;
};
static unsigned events,draws,starts,ends,disabled;
static const tu_image_view *seen_view;
static unsigned seen_format;
static void trace_start_gmem_load(int *,tu_cs *,tu_cmd_buffer *,unsigned,bool){}
static void trace_end_gmem_load(int *,tu_cs *){}
static void tu_begin_load_store_cond_exec(tu_cmd_buffer *,tu_cs *,bool){starts++;}
static void tu_end_load_store_cond_exec(tu_cmd_buffer *,tu_cs *,bool){ends++;}
static void tu_disable_draw_states(tu_cmd_buffer *,tu_cs *){disabled++;}
static bool blit_can_resolve(unsigned){return true;}
static const void *tu_framebuffer_get_tiling_config(tu_framebuffer *,tu_device *,Pass *,unsigned,unsigned){return nullptr;}
template<chip> static void load_3d_blit(tu_cmd_buffer *,tu_cs *,const tu_image_view *view,const tu_render_pass_attachment *att,bool) {
    draws++;seen_view=view;seen_format=att->format;
}
template<chip> static void tu_emit_blit(tu_cmd_buffer *,tu_cs *,tu_resolve_group *,const tu_image_view *view,const tu_render_pass_attachment *att,
                                      const void *,unsigned,bool,bool,bool) {
    events++;seen_view=view;seen_format=att->format;
}
'''
checks = r'''
int main() {
    fd_dev_info info;tu_physical_device physical{&info};Autotune autotune;tu_device device{&physical,&autotune};
    unsigned tested=0,wrong_load=0,wrong_store=0,wrong_layout=0;
    for(unsigned format:{9u,16u,44u,97u}) // Vulkan R8, RG8, BGRA8, RGBA16Float: no pixel reinterpretation.
    for(unsigned tile:{unsigned(TILE6_LINEAR),unsigned(TILE6_3)})
    for(unsigned owner:{0u,1u,2u,3u})
    for(VkExtent2D size: {VkExtent2D{16,4},VkExtent2D{17,5},VkExtent2D{64,32},VkExtent2D{63,31}}) {
        tu_image image;image.layout[0].tile_mode=tile;
        if(owner==1)image.vk.external_handle_types=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        if(owner==2)image.vk.external_handle_types=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT;
        if(owner==3)image.vk.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        tu_image_view view{};view.image=&image;view.view.width=size.width;view.view.height=size.height;
        tu_image_view *views[]={&view};tu_render_pass_attachment att;att.format=format;Pass pass;pass.attachments=&att;
        tu_cmd_buffer cmd;cmd.device=&device;cmd.state.pass=&pass;cmd.state.attachments=views;cmd.state.render_areas[0].extent=size;
        tu_cs cs;tu_resolve_group group;
        const bool tight=tile==TILE6_LINEAR && owner && (size.width%16 || size.height%4);
        events=draws=starts=ends=disabled=0;seen_view=nullptr;
        tu_load_gmem_attachment<A7XX>(&cmd,&cs,&group,0,0,false,true,false);
        wrong_load+=draws!=unsigned(tight) || events!=unsigned(!tight);
        wrong_store+=tu_attachment_store_unaligned(&cmd,0)!=tight;
        tu_choose_gmem_layout(&cmd);
        wrong_layout+=cmd.state.gmem_layout!=unsigned(tight?TU_GMEM_LAYOUT_AVOID_CCU:TU_GMEM_LAYOUT_FULL);
        if(starts!=1 || ends!=1 || seen_view!=&view || seen_format!=format || disabled!=draws)return 2;
        // Partial render areas still use the existing exact-texel store path.
        cmd.state.render_areas[0].offset={1,1};cmd.state.render_areas[0].extent={size.width-1,size.height-1};
        if(!tu_attachment_store_unaligned(&cmd,0))return 3;
        // No load emits no work; a forced load must run, and the debug path is preserved.
        events=draws=0;att.load=false;
        tu_load_gmem_attachment<A7XX>(&cmd,&cs,&group,0,0,false,false,false);
        if(events || draws)return 4;
        debug_3d=true;
        tu_load_gmem_attachment<A7XX>(&cmd,&cs,&group,0,0,false,false,true);
        if(events || draws!=1)return 5;
        debug_3d=false;debug_unaligned=true;
        cmd.state.render_areas[0]={{0,0},size};if(!tu_attachment_store_unaligned(&cmd,0))return 6;
        debug_unaligned=false;tested++;
    }
    std::printf("%s actual GMEM decisions: %u cases, wrong load=%u store=%u layout=%u; tight imports preserve exact-texel paths, aligned/owned/tiled and conditional/forced/debug paths checked; GPU pixels not executed\n",
        wrong_load || wrong_store || wrong_layout?"FAIL":"PASS",tested,wrong_load,wrong_store,wrong_layout);
    return wrong_load || wrong_store || wrong_layout?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='imported-gmem-', dir=os.environ.get('TMPDIR')) as td:
    td = Path(td)
    cpp = td/'test.cpp'
    cpp.write_text(fixture+'\n'.join(parts)+checks)
    binary = td/'test'
    command = ['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
               '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(binary)]
    v = subprocess.run(command, text=True, capture_output=True, timeout=60)
    (a.output_dir/'compile.log').write_text(v.stdout+v.stderr)
    if v.returncode:
        raise SystemExit(v.stdout+v.stderr)
    v = subprocess.run([binary], text=True, capture_output=True, timeout=30)
    (a.output_dir/'test.log').write_text(v.stdout+v.stderr)
    print(v.stdout+v.stderr, end='')
    raise SystemExit(v.returncode)

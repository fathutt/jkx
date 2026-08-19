/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "tr_local.h"
#include "vk_shader_pak.h"

// SPIR-V now comes from base/shaders.pak, built by tools/shadergen/shadergen.py.
// It used to come from shaders/spirv/shader_data.c: 61.7 MB and 1.9 million
// lines of generated C, committed to git and compiled into this translation
// unit at a cost of 32.7 s and 1.5 GB of compiler memory.
//
// The slot table below is still generated, but it is only the mapping from a
// vk.shaders slot to a variant name; the bytes live in the pak.

static void *s_pakData;
static ShaderPak s_pak;

static void vk_open_shader_pak( void )
{
	if ( s_pak.isOpen() ) {
		return;
	}

	const long size = FS_ReadFile( "shaders.pak", &s_pakData );
	if ( s_pakData == NULL || size <= 0 ) {
		Com_Error( ERR_FATAL, "Vulkan: shaders.pak is missing. It is produced by the build; "
			"see tools/shadergen/shadergen.py" );
		return;
	}

	if ( !s_pak.open( s_pakData, (size_t)size ) ) {
		FS_FreeFile( s_pakData );
		s_pakData = NULL;
		Com_Error( ERR_FATAL, "Vulkan: shaders.pak is corrupt or was built for another version" );
	}
}

static void vk_close_shader_pak( void )
{
	s_pak.close();
	if ( s_pakData != NULL ) {
		FS_FreeFile( s_pakData );
		s_pakData = NULL;
	}
}

static VkShaderModule vk_shader_module( const char *name )
{
	size_t size = 0;
	const uint32_t *code = s_pak.find( name, &size );

	if ( code == NULL ) {
		// A missing variant means the manifest and this file disagree, which is
		// a build error rather than something to limp along with.
		Com_Error( ERR_FATAL, "Vulkan: shader variant '%s' is not in shaders.pak", name );
		return VK_NULL_HANDLE;
	}

	VkShaderModuleCreateInfo desc;
	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.codeSize = size;
	desc.pCode = code;

	VkShaderModule module;
	VK_CHECK( vkCreateShaderModule( vk.device, &desc, NULL, &module ) );
	VK_SET_OBJECT_NAME( module, name, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );

	return module;
}

// Keeps every hand-written binding line below unchanged: the identifier that
// used to name a C array now names a pak entry.
#define SHADER_MODULE( name ) vk_shader_module( #name )

static void vk_bind_generated_shaders( void )
{
#define JKX_SHADER_SLOT( slot, name ) slot = vk_shader_module( name );
#include "shader_slots.inl"
#undef JKX_SHADER_SLOT
}

void vk_create_shader_modules( void )
{
    vk_open_shader_pak();

    vk_bind_generated_shaders();

#if 0
    int i, j, k, l, m, n, o;

    const char *vbo[] = { "cpu ", "gpu ghoul2", "gpu mdv" };
    const char *pbr[] = { "", "pbr " };
    const char *light[] = { "", "lightmap", "vector", "vertex" };
    const char *tx[]  = { "single", "double", "triple" };
    const char *cl[]  = { "", "+cl" };
    const char *env[] = { "", "+env" };
    const char *fog[] = { "", "+fog" };

    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 2; j++ ) {
            for ( k = 0; k < 4; k++ ) {
                for ( l = 0; l < 3; l++ ) {
                    for ( m = 0; m < 2; m++ ) {
                        for ( n = 0; n < 2; n++ ) {
                            for ( o = 0; o < 2; o++ ) 
                            {
                                const char *s = va( "%s texture %s%s%s%s%s%s vertex module", vbo[i], pbr[j], light[k], tx[l], cl[m], env[n], fog[o] );
                                VK_SET_OBJECT_NAME( vk.shaders.vert.gen[i][j][k][l][m][n][o], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
                            }
                        }
                    }
                }
            }
        }
    }
    for ( j = 0; j < 2; j++ ) {
        for ( k = 0; k < 4; k++ ) {
            for ( l = 0; l < 3; l++ ) {
                for ( m = 0; m < 2; m++ ) {
                    for ( n = 0; n < 2; n++ ) {
                        const char *s = va( "texture %s%s%s%s%s fragment module", pbr[j], light[k], tx[l], cl[m], fog[n] );
                        VK_SET_OBJECT_NAME( vk.shaders.frag.gen[j][k][l][m][n], s, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT );
                    }
                }
            }
        }
    }
#endif

#ifdef VK_COMPUTE_NORMALMAP
    vk.shaders.normalmap = SHADER_MODULE(normalmap_comp_spv);
#endif

#ifdef USE_VBO_SS
    vk.shaders.surface_sprite_fs[0] = SHADER_MODULE(frag_surface_sprites);
    vk.shaders.surface_sprite_fs[1] = SHADER_MODULE(frag_surface_sprites_fog);
    VK_SET_OBJECT_NAME(vk.shaders.surface_sprite_fs[0], "surface sprite fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.surface_sprite_fs[1], "surface sprite fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.surface_sprite_vs[0] = SHADER_MODULE(vert_surface_sprites);
    vk.shaders.surface_sprite_vs[1] = SHADER_MODULE(vert_surface_sprites_fog);
    VK_SET_OBJECT_NAME(vk.shaders.surface_sprite_vs[0], "surface sprite vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.surface_sprite_vs[1], "surface sprite fog vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
#endif

    vk.shaders.frag.gen0_df = SHADER_MODULE(frag_tx0_df);
    VK_SET_OBJECT_NAME(vk.shaders.frag.gen0_df, "single-texture df fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.vert.gen0_ident = SHADER_MODULE(vert_tx0_ident);
    vk.shaders.frag.gen0_ident = SHADER_MODULE(frag_tx0_ident);
    VK_SET_OBJECT_NAME(vk.shaders.vert.gen0_ident, "single-texture ident.color vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.frag.gen0_ident, "single-texture ident.color fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.vert.light[0] = SHADER_MODULE(vert_light);
    vk.shaders.vert.light[1] = SHADER_MODULE(vert_light_fog);
    VK_SET_OBJECT_NAME(vk.shaders.vert.light[0], "light vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.vert.light[1], "light fog vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.frag.light[0][0] = SHADER_MODULE(frag_light);
    vk.shaders.frag.light[0][1] = SHADER_MODULE(frag_light_fog);
    vk.shaders.frag.light[1][0] = SHADER_MODULE(frag_light_line);
    vk.shaders.frag.light[1][1] = SHADER_MODULE(frag_light_line_fog);
    VK_SET_OBJECT_NAME(vk.shaders.frag.light[0][0], "light fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.frag.light[0][1], "light fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.frag.light[1][0], "linear light fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.frag.light[1][1], "linear light fog fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    // note: both refraction halves come from the generated slot table above

    vk.shaders.color_fs = SHADER_MODULE(color_frag_spv);
    vk.shaders.color_vs = SHADER_MODULE(color_vert_spv);
    VK_SET_OBJECT_NAME(vk.shaders.color_vs, "single-color vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.color_fs, "single-color fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.msdf_fs = SHADER_MODULE(msdf_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.msdf_fs, "distance field text fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.skycube_vs = SHADER_MODULE(skycube_vert_spv);
    VK_SET_OBJECT_NAME(vk.shaders.skycube_vs, "sky cubemap vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    vk.shaders.skycube_fs = SHADER_MODULE(skycube_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.skycube_fs, "sky cubemap fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.dot_vs = SHADER_MODULE(dot_vert_spv);
    vk.shaders.dot_fs = SHADER_MODULE(dot_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.dot_vs, "dot vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.dot_fs, "dot fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.bloom_fs = SHADER_MODULE(bloom_frag_spv);
    vk.shaders.blur_fs = SHADER_MODULE(blur_frag_spv);
    vk.shaders.blend_fs = SHADER_MODULE(blend_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.bloom_fs, "bloom extraction fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.blur_fs, "gaussian blur fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.blend_fs, "final bloom blend fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.gamma_fs = SHADER_MODULE(gamma_frag_spv);
    vk.shaders.gamma_vs = SHADER_MODULE(gamma_vert_spv);
    VK_SET_OBJECT_NAME(vk.shaders.gamma_fs, "gamma post-processing fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
    VK_SET_OBJECT_NAME(vk.shaders.gamma_vs, "gamma post-processing vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

#ifdef VK_PBR_BRDFLUT
    vk.shaders.brdflut_fs = SHADER_MODULE(brdflut_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.brdflut_fs, "brdf LUT fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);
#endif

    vk.shaders.filtercube_vs = SHADER_MODULE(filtercube_vert_spv);
    VK_SET_OBJECT_NAME(vk.shaders.filtercube_vs, "filter cube vertex module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.prefilterenvmap_fs = SHADER_MODULE(prefilterenvmap_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.prefilterenvmap_fs, "prefilter env map fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.irradiancecube_fs = SHADER_MODULE(irradiancecube_frag_spv);
    VK_SET_OBJECT_NAME(vk.shaders.irradiancecube_fs, "irradiance cube fragment module", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk.shaders.filtercube_gm = SHADER_MODULE(filtercube_geom_spv);
    VK_SET_OBJECT_NAME(vk.shaders.filtercube_gm, "filter cube geometry shader", VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT);

    vk_close_shader_pak();
}

void vk_destroy_shader_modules( void )
{
    int i, j, k, l, m, n, o;

    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 2; j++ ) {
            for ( k = 0; k < 4; k++ ) {
                for ( l = 0; l < 3; l++ ) {
                    for ( m = 0; m < 2; m++ ) {
                        for ( n = 0; n < 2; n++ ) {
                            for ( o = 0; o < 2; o++ ) {
                                if ( vk.shaders.vert.gen[i][j][k][l][m][n][o] != VK_NULL_HANDLE ) {
                                    vkDestroyShaderModule( vk.device, vk.shaders.vert.gen[i][j][k][l][m][n][o], NULL );
                                    vk.shaders.vert.gen[i][j][k][l][m][n][o] = VK_NULL_HANDLE;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for ( j = 0; j < 2; j++ ) {
        for ( k = 0; k < 4; k++ ) {
            for ( l = 0; l < 3; l++ ) {
                for ( m = 0; m < 2; m++ ) {
                    for ( n = 0; n < 2; n++ ) {
                        if ( vk.shaders.frag.gen[j][k][l][m][n] != VK_NULL_HANDLE ) {
                            vkDestroyShaderModule( vk.device, vk.shaders.frag.gen[j][k][l][m][n], NULL );
                            vk.shaders.frag.gen[j][k][l][m][n] = VK_NULL_HANDLE;
                        }
                    }
                }
            }
        }
    }
    

    for (i = 0; i < 2; i++) {
        if (vk.shaders.vert.light[i] != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vk.device, vk.shaders.vert.light[i], NULL);
            vk.shaders.vert.light[i] = VK_NULL_HANDLE;
        }
        for (j = 0; j < 2; j++) {
            if (vk.shaders.frag.light[i][j] != VK_NULL_HANDLE) {
                vkDestroyShaderModule(vk.device, vk.shaders.frag.light[i][j], NULL);
                vk.shaders.frag.light[i][j] = VK_NULL_HANDLE;
            }
        }
    }

    for (i = 0; i < 2; i++) {
        if (vk.shaders.frag.fog[i] != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vk.device, vk.shaders.frag.fog[i], NULL);
            vk.shaders.frag.fog[i] = VK_NULL_HANDLE;
        }

        for (j = 0; j < 3; j++) {
            if (vk.shaders.vert.fog[j][i] != VK_NULL_HANDLE) {
                vkDestroyShaderModule(vk.device, vk.shaders.vert.fog[j][i], NULL);
                vk.shaders.vert.fog[j][i] = VK_NULL_HANDLE;
            }
        }
    }

    vkDestroyShaderModule(vk.device, vk.shaders.vert.gen0_ident, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.frag.gen0_ident, NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.frag.gen0_df, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.msdf_fs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.skycube_vs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.skycube_fs, NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.color_fs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.color_vs, NULL);

    for ( i = 0; i < 3; i++ )
        vkDestroyShaderModule(vk.device, vk.shaders.refraction_vs[i], NULL);

    for ( i = 0; i < 2; i++ )
        vkDestroyShaderModule(vk.device, vk.shaders.refraction_fs[i], NULL);

    for ( i = 0; i < 2; i++ )
        vkDestroyShaderModule(vk.device, vk.shaders.depth_resolve_fs[i], NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.liquid_vs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.liquid_fs, NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.dot_vs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.dot_fs, NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.bloom_fs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.blur_fs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.blend_fs, NULL);

    vkDestroyShaderModule(vk.device, vk.shaders.gamma_vs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.gamma_fs, NULL);

#ifdef VK_PBR_BRDFLUT
    vkDestroyShaderModule(vk.device, vk.shaders.brdflut_fs, NULL);
#endif

    vkDestroyShaderModule(vk.device, vk.shaders.filtercube_vs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.filtercube_gm, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.prefilterenvmap_fs, NULL);
    vkDestroyShaderModule(vk.device, vk.shaders.irradiancecube_fs, NULL);

#ifdef VK_COMPUTE_NORMALMAP
    // Created next to the rest and forgotten here, so it outlived the device.
    // Validation names it at vkDestroyDevice; nothing else ever would.
    vkDestroyShaderModule(vk.device, vk.shaders.normalmap, NULL);
    vk.shaders.normalmap = VK_NULL_HANDLE;
#endif

#ifdef USE_VBO_SS
    for ( i = 0; i < 2; i++ ) {
        vkDestroyShaderModule(vk.device, vk.shaders.surface_sprite_fs[i], NULL);
        vkDestroyShaderModule(vk.device, vk.shaders.surface_sprite_vs[i], NULL);
    }
 #endif
}
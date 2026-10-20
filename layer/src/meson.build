subdir('shader')

vkBasalt_src = [
    'basalt.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'descriptor_set.cpp',
    'effect_postfx.cpp',
    'effect_simple.cpp',
    'effect_transfer.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'frame_stats.cpp',
    'graphics_pipeline.cpp',
    'image_view.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'sampler.cpp',
    'shader.cpp',
    'stream_gate.cpp',
    'util.cpp',
    'vkdispatch.cpp',
]

shared_library('betterframexp', vkBasalt_src, shader_include,
    gnu_symbol_visibility: 'hidden', install : false)

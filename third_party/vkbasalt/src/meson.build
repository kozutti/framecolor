subdir('shader')

vkBasalt_src = [
    'basalt.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'descriptor_set.cpp',
    'effect_lut.cpp',
    'effect_simple.cpp',
    'effect_transfer.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'sampler.cpp',
    'shader.cpp',
    'util.cpp',
    'vkdispatch.cpp',
]

shared_library('vkbasalt', vkBasalt_src, shader_include,
    dependencies: dependency('threads'),
    gnu_symbol_visibility: 'hidden', install : false)

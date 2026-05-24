import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import FileIcon from '../FileIcon.vue'

describe('FileIcon', () => {
  it('renders folder icon when isFolder is true', () => {
    const wrapper = mount(FileIcon, {
      props: { isFolder: true },
    })
    expect(wrapper.find('.file-icon').exists()).toBe(true)
  })

  it('renders image icon for image mime type', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'image/png' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#409eff')
  })

  it('renders video icon for video mime type', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'video/mp4' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#9b59b6')
  })

  it('renders audio icon for audio mime type', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'audio/mpeg' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#e67e22')
  })

  it('renders pdf icon for application/pdf', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'application/pdf' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#e74c3c')
  })

  it('renders archive icon for zip files', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'application/zip' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#f1c40f')
  })

  it('renders document icon for ms office types', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'application/vnd.openxmlformats-officedocument.wordprocessingml.document' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#409eff')
  })

  it('renders document icon for text types', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'text/plain' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#909399')
  })

  it('renders default icon for unknown mime type', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'application/octet-stream' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
    expect(icon.attributes('style')).toContain('#909399')
  })

  it('uses custom size prop', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'text/plain', size: 64 },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.exists()).toBe(true)
  })

  it('uses default size of 32', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'text/plain' },
    })
    expect(wrapper.find('.file-icon').exists()).toBe(true)
  })

  it('handles case-insensitive mime type', () => {
    const wrapper = mount(FileIcon, {
      props: { mimeType: 'IMAGE/PNG' },
    })
    const icon = wrapper.find('.file-icon')
    expect(icon.attributes('style')).toContain('#409eff')
  })
})

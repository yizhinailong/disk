import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import SizeDisplay from '../SizeDisplay.vue'

describe('SizeDisplay', () => {
  it('formats bytes correctly', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 500 } })
    expect(wrapper.text()).toBe('500 B')
  })

  it('formats 0 bytes', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 0 } })
    expect(wrapper.text()).toBe('0 B')
  })

  it('formats kilobytes correctly', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1536 } })
    expect(wrapper.text()).toBe('1.50 KB')
  })

  it('formats megabytes correctly', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1048576 } })
    expect(wrapper.text()).toBe('1.00 MB')
  })

  it('formats gigabytes correctly', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1073741824 } })
    expect(wrapper.text()).toBe('1.00 GB')
  })

  it('formats terabytes correctly', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1099511627776 } })
    expect(wrapper.text()).toBe('1.00 TB')
  })

  it('formats negative bytes as 0 B', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: -100 } })
    expect(wrapper.text()).toBe('0 B')
  })

  it('respects custom precision', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1536, precision: 3 } })
    expect(wrapper.text()).toBe('1.500 KB')
  })

  it('formats exactly 1024 bytes as 1.00 KB', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 1024 } })
    expect(wrapper.text()).toBe('1.00 KB')
  })

  it('rounds bytes to integer for B unit', () => {
    const wrapper = mount(SizeDisplay, { props: { bytes: 512 } })
    expect(wrapper.text()).toBe('512 B')
  })
})

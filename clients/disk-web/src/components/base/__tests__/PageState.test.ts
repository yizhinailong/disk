import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import type { ComponentMountingOptions } from '@vue/test-utils'
import PageState from '../PageState.vue'

const ElStub = (name: string) => ({
  template: `<div class="${name}"><slot /></div>`,
})

type PageStateProps = {
  state: 'loading' | 'empty' | 'error' | 'content'
  emptyText?: string
  errorText?: string
}

function mountPageState(
  props: PageStateProps,
  options: Omit<ComponentMountingOptions<typeof PageState>, 'props'> = {},
) {
  return mount(PageState, {
    props,
    global: {
      stubs: {
        ElSkeleton: ElStub('el-skeleton'),
        ElEmpty: {
          props: ['description'],
          template: `<div class="el-empty">{{ description }}</div>`,
        },
        ElIcon: {
          props: ['size'],
          template: `<div class="el-icon"><slot /></div>`,
        },
        ElButton: {
          template: `<button class="el-button"><slot /></button>`,
        },
        CircleCloseFilled: true,
      },
    },
    ...options,
  })
}

describe('PageState', () => {
  it('renders loading state', () => {
    const wrapper = mountPageState({ state: 'loading' })
    expect(wrapper.find('.page-state__loading').exists()).toBe(true)
  })

  it('renders empty state with default text', () => {
    const wrapper = mountPageState({ state: 'empty' })
    expect(wrapper.find('.page-state__empty').exists()).toBe(true)
  })

  it('renders empty state with custom text', () => {
    const wrapper = mountPageState({ state: 'empty', emptyText: '暂无文件' })
    expect(wrapper.find('.page-state__empty').exists()).toBe(true)
    expect(wrapper.text()).toContain('暂无文件')
  })

  it('renders error state with retry button', () => {
    const wrapper = mountPageState({ state: 'error', errorText: '加载失败' })
    expect(wrapper.find('.page-state__error').exists()).toBe(true)
    expect(wrapper.text()).toContain('加载失败')
    const button = wrapper.find('.page-state__error .el-button')
    expect(button.exists()).toBe(true)
  })

  it('emits retry when retry button clicked', async () => {
    const wrapper = mountPageState({ state: 'error', errorText: 'Error' })
    await wrapper.find('.page-state__error .el-button').trigger('click')
    expect(wrapper.emitted('retry')).toBeTruthy()
    expect(wrapper.emitted('retry')!.length).toBe(1)
  })

  it('renders slot content in content state', () => {
    const wrapper = mountPageState(
      { state: 'content' },
      {
        slots: {
          default: '<div class="custom-content">Hello</div>',
        },
      },
    )
    expect(wrapper.find('.custom-content').exists()).toBe(true)
    expect(wrapper.text()).toContain('Hello')
  })

  it('does not render loading/empty/error divs in content state', () => {
    const wrapper = mountPageState(
      { state: 'content' },
      {
        slots: {
          default: '<p>content</p>',
        },
      },
    )
    expect(wrapper.find('.page-state__loading').exists()).toBe(false)
    expect(wrapper.find('.page-state__empty').exists()).toBe(false)
    expect(wrapper.find('.page-state__error').exists()).toBe(false)
  })
})

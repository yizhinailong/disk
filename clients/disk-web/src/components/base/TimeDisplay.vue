<template>
  <span class="time-display">{{ displayText }}</span>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(defineProps<{
  /** 时间值，ISO 字符串或 Date 对象 */
  time: string | Date
  /** 显示模式：相对时间或绝对时间 */
  format?: 'relative' | 'absolute'
  /** 绝对时间格式，支持 YYYY MM DD HH mm ss 占位符 */
  absoluteFormat?: string
}>(), {
  format: 'relative',
  absoluteFormat: 'YYYY-MM-DD HH:mm',
})

function parseDate(input: string | Date): Date {
  if (input instanceof Date) return input
  const d = new Date(input)
  return isNaN(d.getTime()) ? new Date() : d
}

function padZero(n: number): string {
  return n < 10 ? `0${n}` : `${n}`
}

function formatAbsolute(date: Date, fmt: string): string {
  const year = date.getFullYear().toString()
  const month = padZero(date.getMonth() + 1)
  const day = padZero(date.getDate())
  const hours = padZero(date.getHours())
  const minutes = padZero(date.getMinutes())
  const seconds = padZero(date.getSeconds())

  return fmt
    .replace('YYYY', year)
    .replace('MM', month)
    .replace('DD', day)
    .replace('HH', hours)
    .replace('mm', minutes)
    .replace('ss', seconds)
}

function formatRelative(date: Date): string {
  const now = Date.now()
  const target = date.getTime()
  const diff = now - target

  if (diff < 0) return formatAbsolute(date, 'YYYY-MM-DD')

  const seconds = Math.floor(diff / 1000)
  const minutes = Math.floor(seconds / 60)
  const hours = Math.floor(minutes / 60)
  const days = Math.floor(hours / 24)

  if (seconds < 60) return '刚刚'
  if (minutes < 60) return `${minutes}分钟前`
  if (hours < 24) return `${hours}小时前`
  if (days <= 30) return `${days}天前`

  return formatAbsolute(date, 'YYYY-MM-DD')
}

const displayText = computed(() => {
  const date = parseDate(props.time)
  if (props.format === 'absolute') {
    return formatAbsolute(date, props.absoluteFormat)
  }
  return formatRelative(date)
})
</script>

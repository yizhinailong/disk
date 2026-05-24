<template>
  <el-breadcrumb separator="/" class="breadcrumb-nav">
    <el-breadcrumb-item :to="{ path: '/' }" @click.prevent="navigateTo('/')">
      我的文件
    </el-breadcrumb-item>
    <el-breadcrumb-item
      v-for="(item, index) in items"
      :key="index"
      :to="index < items.length - 1 ? { path: item.to } : undefined"
      @click.prevent="index < items.length - 1 && item.to && navigateTo(item.to)"
    >
      {{ item.label }}
    </el-breadcrumb-item>
  </el-breadcrumb>
</template>

<script setup lang="ts">
import { useRouter } from 'vue-router'

defineProps<{
  /** 面包屑路径项，最后一项为当前页（不可点击） */
  items: ReadonlyArray<{ label: string; to?: string }>
}>()

const router = useRouter()

function navigateTo(to: string) {
  router.push(to)
}
</script>

<style scoped>
.breadcrumb-nav {
  padding: 12px 0;
  font-size: 14px;
}
</style>

<template>
  <div class="pagination-wrapper">
    <el-pagination
      :current-page="page"
      :page-size="pageSize"
      :page-sizes="pageSizes"
      :total="total"
      layout="total, sizes, prev, pager, next"
      background
      @current-change="onPageChange"
      @size-change="onSizeChange"
    />
  </div>
</template>

<script setup lang="ts">
const props = withDefaults(defineProps<{
  /** 总记录数 */
  total: number
  /** 当前页码 */
  page: number
  /** 每页条数 */
  pageSize: number
  /** 可选每页条数 */
  pageSizes?: number[]
}>(), {
  pageSizes: () => [20, 50, 100],
})

const emit = defineEmits<{
  'update:page': [page: number]
  'update:pageSize': [size: number]
  /** 页码或每页条数变化时触发 */
  change: [page: number, pageSize: number]
}>()

function onPageChange(newPage: number) {
  emit('update:page', newPage)
  emit('change', newPage, props.pageSize)
}

function onSizeChange(newSize: number) {
  emit('update:pageSize', newSize)
  emit('update:page', 1)
  emit('change', 1, newSize)
}
</script>

<style scoped>
.pagination-wrapper {
  display: flex;
  justify-content: center;
  padding: 16px 0;
}
</style>

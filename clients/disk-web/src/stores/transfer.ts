import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import SparkMD5 from 'spark-md5';
import type { UploadTask, DownloadTask } from '@/types';
import {
  initUpload,
  uploadChunk,
  completeUpload,
  cancelUpload as cancelUploadApi,
} from '@/api/file';
import { useDownload, saveBlobAsFile } from '@/composables/useDownload';

// ==================== Constants ====================

const CHUNK_SIZE = 5 * 1024 * 1024; // 5MB
const MAX_CHUNK_RETRIES = 3;
const CHUNK_RETRY_DELAY_MS = 1000;

// ==================== AbortController Registry ====================

const abortControllers = new Map<string, AbortController>();

function getOrCreateController(taskId: string): AbortController {
  let ctrl = abortControllers.get(taskId);
  if (!ctrl || ctrl.signal.aborted) {
    ctrl = new AbortController();
    abortControllers.set(taskId, ctrl);
  }
  return ctrl;
}

function abortController(taskId: string): void {
  const ctrl = abortControllers.get(taskId);
  if (ctrl) {
    ctrl.abort();
    abortControllers.delete(taskId);
  }
}

// ==================== MD5 Helpers ====================

function computeFileMD5(file: File, signal?: AbortSignal): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunkCount = Math.ceil(file.size / CHUNK_SIZE) || 1;
    const spark = new SparkMD5.ArrayBuffer();
    const reader = new FileReader();
    let currentChunk = 0;

    if (signal?.aborted) {
      reject(new DOMException('Aborted', 'AbortError'));
      return;
    }

    const onAbort = () => {
      reader.abort();
      reject(new DOMException('Aborted', 'AbortError'));
    };
    signal?.addEventListener('abort', onAbort, { once: true });

    reader.onload = (e) => {
      if (e.target?.result) {
        spark.append(e.target.result as ArrayBuffer);
      }
      currentChunk++;
      if (currentChunk < chunkCount) {
        loadNext();
      } else {
        signal?.removeEventListener('abort', onAbort);
        resolve(spark.end());
      }
    };

    reader.onerror = () => {
      signal?.removeEventListener('abort', onAbort);
      reject(new Error('FileReader error during MD5 computation'));
    };

    function loadNext() {
      const start = currentChunk * CHUNK_SIZE;
      const end = Math.min(start + CHUNK_SIZE, file.size);
      reader.readAsArrayBuffer(file.slice(start, end));
    }

    loadNext();
  });
}

function computeChunkMD5(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = (e) => {
      if (e.target?.result) {
        const spark = new SparkMD5.ArrayBuffer();
        spark.append(e.target.result as ArrayBuffer);
        resolve(spark.end());
      } else {
        reject(new Error('Empty chunk data'));
      }
    };
    reader.onerror = () => reject(new Error('FileReader error on chunk'));
    reader.readAsArrayBuffer(blob);
  });
}

// ==================== Helpers ====================

function delay(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}

function findTaskIn(tasks: UploadTask[], taskId: string): UploadTask | undefined {
  return tasks.find((t) => t.id === taskId);
}

// ==================== Store ====================

export const useTransferStore = defineStore('transfer', () => {
  // ==================== State ====================
  const uploads = ref<UploadTask[]>([]);
  const downloads = ref<DownloadTask[]>([]);
  const maxConcurrentUploads = ref<number>(3);

  // ==================== Getters ====================
  const activeUploads = computed(() =>
    uploads.value.filter((t) =>
      ['uploading', 'hashing', 'completing'].includes(t.status),
    ),
  );

  const activeDownloads = computed(() =>
    downloads.value.filter((t) => t.status === 'downloading'),
  );

  const pendingUploads = computed(() =>
    uploads.value.filter((t) => t.status === 'queued'),
  );

  const failedUploads = computed(() =>
    uploads.value.filter((t) => t.status === 'failed'),
  );

  const completedUploads = computed(() =>
    uploads.value.filter((t) => t.status === 'completed'),
  );

  const totalUploadProgress = computed(() => {
    const active = activeUploads.value;
    if (active.length === 0) return 0;
    const sum = active.reduce((acc, t) => acc + t.progress, 0);
    return Math.round(sum / active.length);
  });

  const hasActiveTransfers = computed(
    () => activeUploads.value.length + activeDownloads.value.length > 0,
  );

  // ==================== Internal: process single task ====================

  async function processTask(task: UploadTask): Promise<void> {
    const controller = getOrCreateController(task.id);
    const signal = controller.signal;

    try {
      // Phase 1: Hash
      updateTask(task.id, { status: 'hashing', progress: 0 });

      const fileHash = await computeFileMD5(task.file, signal);
      if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

      // Phase 2: Init upload
      const initResp = await initUpload({
        filename: task.file_name,
        file_size: task.file_size,
        file_hash: fileHash,
        parent_id: task.parent_id,
      });

      // Instant upload (秒传)
      if (initResp.instant_upload) {
        updateTask(task.id, { status: 'completed', progress: 100 });
        abortControllers.delete(task.id);
        return;
      }

      if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

      const uploadId = initResp.upload_id;
      const totalChunks = initResp.total_chunks;
      const alreadyUploaded = new Set(initResp.uploaded_chunks);

      updateTask(task.id, {
        upload_id: uploadId,
        uploaded_chunks: alreadyUploaded,
        status: 'uploading',
      });

      // Phase 3: Upload chunks (skip already uploaded for resume)
      const remaining: number[] = [];
      for (let i = 0; i < totalChunks; i++) {
        if (!alreadyUploaded.has(i)) {
          remaining.push(i);
        }
      }

      // Calculate initial progress from resumed chunks
      if (alreadyUploaded.size > 0 && remaining.length === 0) {
        // All chunks already uploaded — skip straight to complete
        updateTask(task.id, { progress: 99 });
      } else if (alreadyUploaded.size > 0) {
        const resumedProgress = Math.round((alreadyUploaded.size / totalChunks) * 100);
        updateTask(task.id, { progress: resumedProgress });
      }

      for (const chunkIndex of remaining) {
        if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

        const start = chunkIndex * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, task.file_size);
        const blob = task.file.slice(start, end);
        const chunkHash = await computeChunkMD5(blob);

        // Retry loop for single chunk
        let uploaded = false;
        for (let attempt = 1; attempt <= MAX_CHUNK_RETRIES; attempt++) {
          if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

          try {
            await uploadChunk(uploadId, chunkIndex, chunkHash, blob);
            uploaded = true;
            break;
          } catch (err) {
            if (signal.aborted) throw new DOMException('Aborted', 'AbortError');
            if (attempt === MAX_CHUNK_RETRIES) throw err;
            await delay(CHUNK_RETRY_DELAY_MS * attempt);
          }
        }

        if (uploaded) {
          // Update uploaded chunks set
          const currentTask = findTaskIn(uploads.value, task.id);
          if (currentTask) {
            const newSet = new Set(currentTask.uploaded_chunks);
            newSet.add(chunkIndex);
            const progress = Math.round((newSet.size / totalChunks) * 100);
            updateTask(task.id, {
              uploaded_chunks: newSet,
              progress: Math.min(progress, 99),
            });
          }
        }
      }

      if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

      // Phase 4: Complete upload
      updateTask(task.id, { status: 'completing', progress: 99 });
      await completeUpload({ upload_id: uploadId });

      if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

      updateTask(task.id, { status: 'completed', progress: 100 });
      abortControllers.delete(task.id);
    } catch (err: unknown) {
      if (err instanceof DOMException && err.name === 'AbortError') {
        updateTask(task.id, { status: 'cancelled', error: '已取消' });
        return;
      }
      const message = err instanceof Error ? err.message : '上传失败';
      updateTask(task.id, { status: 'failed', error: message });
      abortControllers.delete(task.id);
    }
  }

  function updateTask(
    taskId: string,
    patch: Partial<Pick<UploadTask, 'status' | 'progress' | 'upload_id' | 'uploaded_chunks' | 'error'>>,
  ): void {
    const idx = uploads.value.findIndex((t) => t.id === taskId);
    if (idx === -1) return;
    const existing = uploads.value[idx]!;
    uploads.value[idx] = {
      ...existing,
      ...patch,
    };
  }

  // ==================== Upload Queue ====================

  let queueRunning = false;

  function startUploadQueue(): void {
    if (queueRunning) return;
    queueRunning = true;
    drainQueue();
  }

  async function drainQueue(): Promise<void> {
    while (true) {
      const activeCount = uploads.value.filter((t) =>
        ['uploading', 'hashing', 'completing'].includes(t.status),
      ).length;

      if (activeCount >= maxConcurrentUploads.value) {
        // Wait a tick and check again
        await delay(200);
        continue;
      }

      const next = uploads.value.find((t) => t.status === 'queued');
      if (!next) {
        // No more queued tasks — stop the queue
        queueRunning = false;
        return;
      }

      // Fire and forget — processTask handles its own errors
      processTask(next).then(() => {
        // After task completes, continue draining if needed
        if (!queueRunning) {
          startUploadQueue();
        }
      });
      // Small delay to let the task start before checking active count again
      await delay(50);
    }
  }

  // ==================== Public Actions ====================

  async function addUploadTask(file: File, parentId: number): Promise<void> {
    const totalChunks = Math.ceil(file.size / CHUNK_SIZE) || 1;

    const task: UploadTask = {
      id: crypto.randomUUID(),
      file,
      file_name: file.name,
      file_size: file.size,
      status: 'queued',
      progress: 0,
      uploaded_chunks: new Set<number>(),
      total_chunks: totalChunks,
      parent_id: parentId,
    };

    uploads.value.push(task);
    startUploadQueue();
  }

  function removeUploadTask(taskId: string): void {
    abortController(taskId);
    uploads.value = uploads.value.filter((t) => t.id !== taskId);
  }

  async function cancelUploadTask(taskId: string): Promise<void> {
    const task = findTaskIn(uploads.value, taskId);
    if (!task) return;

    // Abort local operations
    abortController(taskId);

    // If server-side upload was initialized, cancel on server
    if (task.upload_id && task.status !== 'completed' && task.status !== 'cancelled') {
      try {
        await cancelUploadApi(task.upload_id);
      } catch {
        // Best-effort server cancellation — local state already updated
      }
    }

    updateTask(taskId, { status: 'cancelled', error: '已取消' });
  }

  async function retryUploadTask(taskId: string): Promise<void> {
    const task = findTaskIn(uploads.value, taskId);
    if (!task || task.status !== 'failed') return;

    // Reset task state for retry
    updateTask(taskId, {
      status: 'queued',
      progress: 0,
      error: undefined,
    });
    // Create fresh abort controller
    getOrCreateController(taskId);
    startUploadQueue();
  }

  function updateDownloadTask(
    taskId: string,
    patch: Partial<Pick<DownloadTask, 'status' | 'progress' | 'error'>>,
  ): void {
    const idx = downloads.value.findIndex((t) => t.id === taskId);
    if (idx === -1) return;
    const existing = downloads.value[idx]!;
    downloads.value[idx] = { ...existing, ...patch };
  }

  async function executeDownload(task: DownloadTask): Promise<void> {
    const controller = getOrCreateController(task.id);
    const signal = controller.signal;

    try {
      updateDownloadTask(task.id, { status: 'downloading', progress: 0 });

      const { startDownload } = useDownload();
      const { blob, filename } = await startDownload(Number(task.file_id), {
        signal,
        onProgress: (_loaded, _total, progress) => {
          updateDownloadTask(task.id, { progress: Math.min(progress, 99) });
        },
      });

      if (signal.aborted) throw new DOMException('Aborted', 'AbortError');

      saveBlobAsFile(blob, filename);
      updateDownloadTask(task.id, { status: 'completed', progress: 100 });
      abortControllers.delete(task.id);
    } catch (err: unknown) {
      if (err instanceof DOMException && err.name === 'AbortError') {
        return;
      }
      const message = err instanceof Error ? err.message : '下载失败';
      updateDownloadTask(task.id, { status: 'failed', error: message });
      abortControllers.delete(task.id);
    }
  }

  function addDownloadTask(fileId: number, fileName: string, fileSize: number): void {
    const task: DownloadTask = {
      id: crypto.randomUUID(),
      file_id: String(fileId),
      file_name: fileName,
      file_size: fileSize,
      status: 'pending',
      progress: 0,
    };

    downloads.value.push(task);
    executeDownload(task);
  }

  function removeDownloadTask(taskId: string): void {
    downloads.value = downloads.value.filter((t) => t.id !== taskId);
  }

  function cancelDownloadTask(taskId: string): void {
    abortController(taskId);
    updateDownloadTask(taskId, { status: 'cancelled', error: '已取消' });
  }

  function pauseDownloadTask(taskId: string): void {
    abortController(taskId);
    updateDownloadTask(taskId, { status: 'paused' });
  }

  function resumeDownloadTask(taskId: string): void {
    const task = downloads.value.find((t) => t.id === taskId);
    if (!task || task.status !== 'paused') return;

    updateDownloadTask(taskId, { status: 'pending', progress: 0 });
    getOrCreateController(taskId);
    executeDownload({ ...task, status: 'pending', progress: 0 });
  }

  function clearCompletedTasks(): void {
    uploads.value = uploads.value.filter(
      (t) => t.status !== 'completed' && t.status !== 'cancelled',
    );
    downloads.value = downloads.value.filter(
      (t) => t.status !== 'completed' && t.status !== 'cancelled',
    );
  }

  function clearCompletedUploads(): void {
    uploads.value = uploads.value.filter(
      (t) => t.status !== 'completed' && t.status !== 'cancelled',
    );
  }

  function clearCompletedDownloads(): void {
    downloads.value = downloads.value.filter(
      (t) => t.status !== 'completed' && t.status !== 'cancelled' && t.status !== 'failed',
    );
  }

  return {
    // state
    uploads,
    downloads,
    maxConcurrentUploads,
    // getters
    activeUploads,
    activeDownloads,
    pendingUploads,
    failedUploads,
    completedUploads,
    totalUploadProgress,
    hasActiveTransfers,
    // actions
    addUploadTask,
    removeUploadTask,
    cancelUploadTask,
    retryUploadTask,
    startUploadQueue,
    addDownloadTask,
    removeDownloadTask,
    cancelDownloadTask,
    pauseDownloadTask,
    resumeDownloadTask,
    clearCompletedTasks,
    clearCompletedUploads,
    clearCompletedDownloads,
  };
});

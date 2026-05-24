import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import * as userApi from '@/api/user';
import type { UserProfile, StorageResponse, UpdateProfileRequest } from '@/types';

export const useProfileStore = defineStore('profile', () => {
  // ==================== State ====================
  const profile = ref<UserProfile | null>(null);
  const storageStats = ref<StorageResponse | null>(null);
  const loading = ref<boolean>(false);

  // ==================== Getters ====================
  const storagePercentage = computed(
    () => storageStats.value?.percentage ?? 0,
  );

  const quotaFormatted = computed(() => {
    if (!storageStats.value) return '0 B / 0 B';
    const used = formatBytes(storageStats.value.used);
    const quota = formatBytes(storageStats.value.quota);
    return `${used} / ${quota}`;
  });

  // ==================== Actions ====================
  async function fetchProfile(): Promise<void> {
    loading.value = true;
    try {
      const result = await userApi.getProfile();
      profile.value = { ...result.user };
    } finally {
      loading.value = false;
    }
  }

  async function updateProfile(data: UpdateProfileRequest): Promise<void> {
    loading.value = true;
    try {
      const result = await userApi.updateProfile(data);
      profile.value = { ...result.user };
    } finally {
      loading.value = false;
    }
  }

  async function changePassword(
    oldPassword: string,
    newPassword: string,
  ): Promise<void> {
    await userApi.changePassword({
      old_password: oldPassword,
      new_password: newPassword,
    });
  }

  async function fetchStorageStats(): Promise<void> {
    const result = await userApi.getStorageStats();
    storageStats.value = { ...result };
  }

  return {
    // state
    profile,
    storageStats,
    loading,
    // getters
    storagePercentage,
    quotaFormatted,
    // actions
    fetchProfile,
    updateProfile,
    changePassword,
    fetchStorageStats,
  };
});

/** Format bytes into human-readable string */
function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.min(
    Math.floor(Math.log(bytes) / Math.log(1024)),
    units.length - 1,
  );
  return `${(bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

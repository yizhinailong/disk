export { apiClient, publicClient, createShareClient, ApiError } from './client'
export { getAccessToken, getRefreshToken, setTokens, clearTokens } from './client'

export { register, login, refreshToken, logout } from './auth'
export { getProfile, updateProfile, changePassword, getStorageStats } from './user'
export {
  initUpload,
  uploadChunk,
  completeUpload,
  cancelUpload,
  listFiles,
  getFileDetail,
  getDownloadInfo,
  downloadFile,
  renameFile,
  moveFiles,
  copyFiles,
  deleteFiles,
  searchFiles,
} from './file'
export { createFolder, getFolderTree, getBreadcrumb } from './folder'
export {
  createShare,
  listShares,
  getShareDetail,
  updateShare,
  cancelShares,
  accessShare,
  browseShare,
  downloadShareFile,
} from './share'
export { listTrash, restoreTrash, deleteTrash, deleteAllTrash } from './trash'
export {
  listUsers,
  getUserDetail,
  changeUserStatus,
  changeUserRole,
  deleteUser,
  getStorageStats as getAdminStorageStats,
  listShares as listAdminShares,
  getShareDetail as getAdminShareDetail,
  deleteShare as deleteAdminShare,
  getStatsOverview,
  getStatsSystem,
  listLogs as listAdminLogs,
} from './admin'
export { healthCheck, getSystemInfo, getLogs } from './system'

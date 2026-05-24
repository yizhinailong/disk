import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import type { LoginUser, RegisterRequest, UserRole, TokenPayload } from '@/types';
import * as authApi from '@/api/auth';
import { setTokens, clearTokens, getAccessToken, getRefreshToken } from '@/api/client';

function decodeJwtPayload(token: string): TokenPayload | null {
  try {
    const parts = token.split('.');
    if (parts.length !== 3) return null;
    const payload = parts[1]!;
    const base64 = payload.replace(/-/g, '+').replace(/_/g, '/');
    const jsonStr = decodeURIComponent(
      atob(base64)
        .split('')
        .map((c) => '%' + ('00' + c.charCodeAt(0).toString(16)).slice(-2))
        .join(''),
    );
    return JSON.parse(jsonStr) as TokenPayload;
  } catch {
    return null;
  }
}

export const useAuthStore = defineStore('auth', () => {
  // ==================== State ====================
  const accessToken = ref<string | null>(null);
  const refreshToken = ref<string | null>(null);
  const user = ref<LoginUser | null>(null);
  /** Decoded from JWT; LoginUser does not carry role */
  const userRole = ref<UserRole>(0);
  /** Timestamp (seconds) when access token expires */
  const tokenExpiry = ref<number>(0);

  // ==================== Getters ====================
  const isAuthenticated = computed(
    () => accessToken.value !== null && Date.now() < tokenExpiry.value * 1000,
  );

  const isAdmin = computed(() => userRole.value === 1);

  /** Seconds until token expires (for refresh threshold check) */
  const tokenExpiresIn = computed(() => {
    if (tokenExpiry.value === 0) return 0;
    const remaining = tokenExpiry.value - Date.now() / 1000;
    return remaining > 0 ? remaining : 0;
  });

  // ==================== Internal helpers ====================

  function applyTokens(access: string, refresh: string): void {
    setTokens(access, refresh);
    accessToken.value = access;
    refreshToken.value = refresh;
    const payload = decodeJwtPayload(access);
    if (payload) {
      userRole.value = payload.role as UserRole;
      tokenExpiry.value = payload.exp;
    }
  }

  // ==================== Actions ====================

  async function login(username: string, password: string): Promise<void> {
    const res = await authApi.login({ account: username, password });
    applyTokens(res.access_token, res.refresh_token);
    user.value = res.user;
  }

  async function register(data: RegisterRequest): Promise<void> {
    await authApi.register(data);
    await login(data.username, data.password);
  }

  async function logout(): Promise<void> {
    try {
      await authApi.logout();
    } catch {
      // logout API failure must not prevent local cleanup
    }
    clearAuth();
    clearTokens();
  }

  async function refreshAccessToken(): Promise<string | null> {
    const rt = refreshToken.value ?? getRefreshToken();
    if (!rt) return null;

    const res = await authApi.refreshToken({ refresh_token: rt });
    applyTokens(res.access_token, res.refresh_token);
    return res.access_token;
  }

  function loadTokensFromStorage(): void {
    const access = getAccessToken();
    const refresh = getRefreshToken();
    if (!access || !refresh) return;

    accessToken.value = access;
    refreshToken.value = refresh;

    const payload = decodeJwtPayload(access);
    if (payload) {
      userRole.value = payload.role as UserRole;
      tokenExpiry.value = payload.exp;
    }
  }

  function clearAuth(): void {
    accessToken.value = null;
    refreshToken.value = null;
    user.value = null;
    userRole.value = 0;
    tokenExpiry.value = 0;
  }

  function setUser(newUser: LoginUser): void {
    user.value = newUser;
  }

  return {
    accessToken,
    refreshToken,
    user,
    userRole,
    tokenExpiry,
    isAuthenticated,
    isAdmin,
    tokenExpiresIn,
    login,
    register,
    logout,
    refreshAccessToken,
    loadTokensFromStorage,
    clearAuth,
    setUser,
  };
});

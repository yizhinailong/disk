package api

import (
	"context"

	"github.com/liufeng/disk/ui/tui/internal/models"
)

type AuthAPI struct {
	client *Client
}

func (a *AuthAPI) Login(ctx context.Context, account, password string) (*models.LoginResponse, error) {
	req := &models.LoginRequest{
		Account:  account,
		Password: password,
	}

	var resp models.LoginResponse
	if err := a.client.doRequest(ctx, "POST", "/api/auth/login", req, &resp); err != nil {
		return nil, err
	}

	a.client.SetToken(resp.AccessToken, resp.RefreshToken, resp.ExpiresIn)

	return &resp, nil
}

func (a *AuthAPI) Logout(ctx context.Context) error {
	err := a.client.doRequest(ctx, "POST", "/api/auth/logout", nil, nil)
	if err == nil {
		a.client.ClearToken()
	}
	return err
}

func (a *AuthAPI) Refresh(ctx context.Context) error {
	a.client.mu.RLock()
	refreshToken := a.client.refreshToken
	a.client.mu.RUnlock()

	if refreshToken == "" {
		return &APIError{Code: 40105, Message: "无刷新令牌"}
	}

	req := &models.RefreshRequest{
		RefreshToken: refreshToken,
	}

	var resp models.RefreshResponse
	if err := a.client.doRequest(ctx, "POST", "/api/auth/refresh", req, &resp); err != nil {
		a.client.ClearToken()
		return err
	}

	a.client.SetToken(resp.AccessToken, resp.RefreshToken, resp.ExpiresIn)

	return nil
}

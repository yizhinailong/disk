package client

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
)

// Register calls POST /api/auth/register.
func (c *Client) Register(ctx context.Context, username, email, password string) (RegisterResponse, error) {
	body, ct, err := writeJSON(map[string]string{
		"username": username,
		"email":    email,
		"password": password,
	})
	if err != nil {
		return RegisterResponse{}, err
	}
	var out RegisterResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/auth/register", requestOpts{
		body: body, contentType: ct, noAuth: true,
	}, &out)
	return out, err
}

// Login calls POST /api/auth/login.
// account may be a username or email.
func (c *Client) Login(ctx context.Context, account, password string) (LoginResponse, error) {
	body, ct, err := writeJSON(map[string]string{
		"account":  account,
		"password": password,
	})
	if err != nil {
		return LoginResponse{}, err
	}
	var out LoginResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/auth/login", requestOpts{
		body: body, contentType: ct, noAuth: true,
	}, &out)
	if err == nil {
		c.SetTokens(out.AccessToken, out.RefreshToken)
	}
	return out, err
}

// Refresh calls POST /api/auth/refresh explicitly.
func (c *Client) Refresh(ctx context.Context, refreshToken string) (RefreshResponse, error) {
	body, ct, err := writeJSON(map[string]string{"refresh_token": refreshToken})
	if err != nil {
		return RefreshResponse{}, err
	}
	var out RefreshResponse
	err = c.decodeEnvelope(ctx, http.MethodPost, "/api/auth/refresh", requestOpts{
		body: body, contentType: ct, noAuth: true,
	}, &out)
	return out, err
}

// Logout calls POST /api/auth/logout and clears local tokens regardless
// of the server response (best-effort).
func (c *Client) Logout(ctx context.Context) error {
	body, ct, err := writeJSON(map[string]any{})
	if err != nil {
		return err
	}
	// buildRequest so we can read the status code; the body may be null.
	req, err := c.buildRequest(ctx, http.MethodPost, "/api/auth/logout", requestOpts{
		body: body, contentType: ct,
	})
	if err != nil {
		c.ClearTokens()
		return err
	}
	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		c.ClearTokens()
		return err
	}
	defer resp.Body.Close()
	// Drain to allow connection reuse.
	_ = drainBody(resp)
	c.ClearTokens()
	if resp.StatusCode != http.StatusOK {
		return &APIError{HTTPCode: resp.StatusCode, URL: "/api/auth/logout"}
	}
	return nil
}

// drainBody reads and discards the response body.
func drainBody(resp *http.Response) error {
	buf := make([]byte, 4096)
	for {
		if _, err := resp.Body.Read(buf); err != nil {
			if err.Error() == "EOF" || err == bytes.ErrTooLarge {
				return nil
			}
			return err
		}
	}
}

// Health calls GET /api/health (public).
func (c *Client) Health(ctx context.Context) (HealthResponse, error) {
	var out HealthResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/health", requestOpts{noAuth: true}, &out)
	if err != nil {
		// Some health endpoints return a non-envelope shape; fall back to raw.
		resp, rerr := c.doRaw(ctx, http.MethodGet, "/api/health", requestOpts{noAuth: true})
		if rerr == nil {
			defer resp.Body.Close()
			_ = json.NewDecoder(resp.Body).Decode(&out)
		}
	}
	return out, err
}

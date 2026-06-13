package client

import (
	"context"
	"net/http"
)

// GetProfile calls GET /api/user/profile.
func (c *Client) GetProfile(ctx context.Context) (User, error) {
	var out User
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/user/profile", requestOpts{}, &out)
	return out, err
}

// UpdateProfile calls PATCH /api/user/profile.
// nickname and avatar may be empty to skip.
func (c *Client) UpdateProfile(ctx context.Context, nickname, avatar string) (User, error) {
	payload := map[string]any{}
	if nickname != "" {
		payload["nickname"] = nickname
	}
	if avatar != "" {
		payload["avatar"] = avatar
	}
	body, ct, err := writeJSON(payload)
	if err != nil {
		return User{}, err
	}
	var out User
	err = c.decodeEnvelope(ctx, http.MethodPatch, "/api/user/profile", requestOpts{
		body: body, contentType: ct,
	}, &out)
	return out, err
}

// ChangePassword calls PUT /api/user/password.
func (c *Client) ChangePassword(ctx context.Context, oldPassword, newPassword string) error {
	body, ct, err := writeJSON(map[string]string{
		"old_password": oldPassword,
		"new_password": newPassword,
	})
	if err != nil {
		return err
	}
	return c.decodeEnvelope(ctx, http.MethodPut, "/api/user/password", requestOpts{
		body: body, contentType: ct,
	}, nil)
}

// GetStorage calls GET /api/user/storage.
func (c *Client) GetStorage(ctx context.Context) (StorageResponse, error) {
	var out StorageResponse
	err := c.decodeEnvelope(ctx, http.MethodGet, "/api/user/storage", requestOpts{}, &out)
	return out, err
}

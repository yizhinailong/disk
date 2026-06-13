// Package client is the Go HTTP client for the disk backend.
// It covers every documented backend endpoint (auth, user, file, folder,
// share, trash, admin, operation log, system, health).
package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
)

// DefaultBaseURL matches the backend dev server (clients/desktop default).
const DefaultBaseURL = "http://127.0.0.1:8080/"

// APIError is returned when the backend envelope reports a non-zero code,
// or when the HTTP round-trip fails.
type APIError struct {
	Code     int
	Message  string
	HTTPCode int
	URL      string
}

func (e *APIError) Error() string {
	if e.Message == "" {
		return fmt.Sprintf("disk api error: code=%d http=%d url=%s", e.Code, e.HTTPCode, e.URL)
	}
	return fmt.Sprintf("disk api error: code=%d http=%d message=%s url=%s", e.Code, e.HTTPCode, e.Message, e.URL)
}

// IsAuthError returns true when the error indicates the access token is
// missing, expired or otherwise invalid (HTTP 401 or backend code 401xx).
func IsAuthError(err error) bool {
	var ae *APIError
	if errors.As(err, &ae) {
		if ae.HTTPCode == http.StatusUnauthorized {
			return true
		}
		// token/auth range — see ErrorCode.hpp
		if ae.Code >= 40100 && ae.Code < 40200 {
			return true
		}
	}
	return false
}

// TokenSource provides the current access token (used by the client to
// retry once after a refresh). Implementations typically wrap Config.
type TokenSource interface {
	AccessToken() string
	RefreshToken() string
	SetTokens(access, refresh string)
}

// Client is the disk HTTP client. It is safe for concurrent use.
type Client struct {
	BaseURL    string
	HTTPClient *http.Client

	mu          sync.RWMutex
	access      string
	refresh     string
	shareToken  string
	tokenSource TokenSource
}

// Option configures a Client.
type Option func(*Client)

// WithHTTPClient supplies a custom *http.Client.
func WithHTTPClient(h *http.Client) Option {
	return func(c *Client) { c.HTTPClient = h }
}

// WithBaseURL overrides the base URL.
func WithBaseURL(u string) Option {
	return func(c *Client) { c.BaseURL = normalizeBaseURL(u) }
}

// WithTokenSource wires a TokenSource so the client can read/persist
// tokens through it. Initial tokens are pulled at first call.
func WithTokenSource(ts TokenSource) Option {
	return func(c *Client) {
		c.tokenSource = ts
		if ts != nil {
			c.access = ts.AccessToken()
			c.refresh = ts.RefreshToken()
		}
	}
}

// New constructs a Client with sensible defaults.
func New(opts ...Option) *Client {
	c := &Client{
		BaseURL:    DefaultBaseURL,
		HTTPClient: &http.Client{Timeout: 60 * time.Second},
	}
	for _, opt := range opts {
		opt(c)
	}
	return c
}

// SetAccessToken sets the JWT access token used on owner requests.
func (c *Client) SetAccessToken(t string) {
	c.mu.Lock()
	c.access = t
	c.mu.Unlock()
	if c.tokenSource != nil {
		c.tokenSource.SetTokens(t, c.refreshTokenLocked())
	}
}

// SetRefreshToken sets the JWT refresh token.
func (c *Client) SetRefreshToken(t string) {
	c.mu.Lock()
	c.refresh = t
	c.mu.Unlock()
	if c.tokenSource != nil {
		c.tokenSource.SetTokens(c.accessTokenLocked(), t)
	}
}

// SetTokens sets both tokens at once.
func (c *Client) SetTokens(access, refresh string) {
	c.mu.Lock()
	c.access = access
	c.refresh = refresh
	c.mu.Unlock()
	if c.tokenSource != nil {
		c.tokenSource.SetTokens(access, refresh)
	}
}

// AccessToken returns the current access token (empty if logged out).
func (c *Client) AccessToken() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.access
}

// RefreshToken returns the current refresh token.
func (c *Client) RefreshToken() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.refresh
}

// SetShareToken sets the visitor share token (X-Share-Token) used when
// browsing/downloading share content.
func (c *Client) SetShareToken(t string) {
	c.mu.Lock()
	c.shareToken = t
	c.mu.Unlock()
}

// ShareToken returns the current visitor share token.
func (c *Client) ShareToken() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.shareToken
}

// ClearShareToken removes any visitor share token.
func (c *Client) ClearShareToken() { c.SetShareToken("") }

func (c *Client) accessTokenLocked() string  { return c.access }
func (c *Client) refreshTokenLocked() string { return c.refresh }

// ClearTokens clears all tokens (does not call the server).
func (c *Client) ClearTokens() {
	c.SetTokens("", "")
	c.SetShareToken("")
}

// IsAuthenticated reports whether an access token is set.
func (c *Client) IsAuthenticated() bool { return c.AccessToken() != "" }

func normalizeBaseURL(u string) string {
	u = strings.TrimSpace(u)
	if u == "" {
		return DefaultBaseURL
	}
	if !strings.HasPrefix(u, "http://") && !strings.HasPrefix(u, "https://") {
		u = "http://" + u
	}
	if !strings.HasSuffix(u, "/") {
		u += "/"
	}
	return u
}

// urlFor builds an absolute URL for a path like "/api/auth/login".
func (c *Client) urlFor(path string) string {
	if strings.HasPrefix(path, "http://") || strings.HasPrefix(path, "https://") {
		return path
	}
	if !strings.HasPrefix(path, "/") {
		path = "/" + path
	}
	return c.BaseURL + strings.TrimPrefix(path, "/")
}

// doRequest applies auth headers and dispatches the request.
func (c *Client) doRequest(ctx context.Context, method, path string, opts requestOpts) (*http.Response, error) {
	req, err := c.buildRequest(ctx, method, path, opts)
	if err != nil {
		return nil, err
	}
	return c.HTTPClient.Do(req)
}

type requestOpts struct {
	body        []byte
	contentType string
	query       url.Values
	headers     map[string]string
	noAuth      bool // skip Authorization header
	shareAuth   bool // include X-Share-Token
	rawReader   io.Reader
}

func (c *Client) buildRequest(ctx context.Context, method, path string, opts requestOpts) (*http.Request, error) {
	full := c.urlFor(path)
	if len(opts.query) > 0 {
		full += "?" + opts.query.Encode()
	}

	var body io.Reader
	if opts.rawReader != nil {
		body = opts.rawReader
	} else if opts.body != nil {
		body = bytes.NewReader(opts.body)
	}

	req, err := http.NewRequestWithContext(ctx, method, full, body)
	if err != nil {
		return nil, err
	}

	if opts.contentType != "" {
		req.Header.Set("Content-Type", opts.contentType)
	}
	if !opts.noAuth {
		if t := c.AccessToken(); t != "" {
			req.Header.Set("Authorization", "Bearer "+t)
		}
	}
	if opts.shareAuth {
		if t := c.ShareToken(); t != "" {
			req.Header.Set("X-Share-Token", t)
		}
	}
	for k, v := range opts.headers {
		req.Header.Set(k, v)
	}
	return req, nil
}

// decodeEnvelope issues a JSON request and decodes the unified envelope.
// If the access token returns 401 (or auth-range code) and we have a
// refresh token, it retries once after refreshing.
func (c *Client) decodeEnvelope(ctx context.Context, method, path string, opts requestOpts, target any) error {
	resp, err := c.doRequest(ctx, method, path, opts)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	bodyBytes, err := io.ReadAll(resp.Body)
	if err != nil {
		return &APIError{HTTPCode: resp.StatusCode, URL: path, Message: "read body: " + err.Error()}
	}

	var env Envelope[json.RawMessage]
	if err := json.Unmarshal(bodyBytes, &env); err != nil {
		return &APIError{
			HTTPCode: resp.StatusCode,
			URL:      path,
			Message:  fmt.Sprintf("invalid json response: %s; body=%s", err.Error(), truncate(string(bodyBytes), 256)),
		}
	}

	if env.Code != CodeSuccess {
		return &APIError{Code: env.Code, HTTPCode: resp.StatusCode, URL: path, Message: env.Message}
	}

	// 401 fallback path — even if envelope parsed, treat HTTP 401 as auth-expired.
	if resp.StatusCode == http.StatusUnauthorized && !opts.noAuth {
		if refreshed, rerr := c.tryRefresh(ctx); rerr == nil && refreshed {
			// rebuild with new token and retry once
			resp2, err2 := c.doRequest(ctx, method, path, opts)
			if err2 != nil {
				return err2
			}
			defer resp2.Body.Close()
			b2, _ := io.ReadAll(resp2.Body)
			var env2 Envelope[json.RawMessage]
			if err := json.Unmarshal(b2, &env2); err != nil {
				return &APIError{HTTPCode: resp2.StatusCode, URL: path, Message: "invalid json on retry"}
			}
			if env2.Code != CodeSuccess {
				return &APIError{Code: env2.Code, HTTPCode: resp2.StatusCode, URL: path, Message: env2.Message}
			}
			return decodeData(env2.Data, target)
		}
	}

	return decodeData(env.Data, target)
}

func decodeData(raw json.RawMessage, target any) error {
	if target == nil {
		return nil
	}
	if len(raw) == 0 || string(raw) == "null" {
		return nil
	}
	if err := json.Unmarshal(raw, target); err != nil {
		return fmt.Errorf("unmarshal response data: %w", err)
	}
	return nil
}

// tryRefresh performs a single refresh attempt using the current refresh
// token. Returns true on success.
func (c *Client) tryRefresh(ctx context.Context) (bool, error) {
	rt := c.RefreshToken()
	if rt == "" {
		return false, errors.New("no refresh token")
	}
	body, _ := json.Marshal(map[string]string{"refresh_token": rt})
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.urlFor("/api/auth/refresh"), bytes.NewReader(body))
	if err != nil {
		return false, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return false, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return false, &APIError{HTTPCode: resp.StatusCode, URL: "/api/auth/refresh"}
	}
	var env Envelope[RefreshResponse]
	if err := json.NewDecoder(resp.Body).Decode(&env); err != nil {
		return false, err
	}
	if env.Code != CodeSuccess || env.Data.AccessToken == "" {
		return false, &APIError{Code: env.Code, Message: env.Message, URL: "/api/auth/refresh"}
	}
	c.SetTokens(env.Data.AccessToken, env.Data.RefreshToken)
	return true, nil
}

// doRaw issues a request and returns the raw HTTP response; caller must
// close the body. Used for binary download endpoints.
func (c *Client) doRaw(ctx context.Context, method, path string, opts requestOpts) (*http.Response, error) {
	return c.doRequest(ctx, method, path, opts)
}

// helpers ------------------------------------------------------------------

// writeJSON marshals v and returns the body + content type.
func writeJSON(v any) ([]byte, string, error) {
	b, err := json.Marshal(v)
	if err != nil {
		return nil, "", err
	}
	return b, "application/json", nil
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}

// readPaginatedJSON parses a JSON body and returns the data field's items
// + pagination. Kept for callers that want to walk raw JSON.
func readPaginatedJSON(body []byte) ([]map[string]any, Pagination, error) {
	var env Envelope[struct {
		Items      []map[string]any `json:"items"`
		Pagination Pagination       `json:"pagination"`
	}]
	if err := json.Unmarshal(body, &env); err != nil {
		return nil, Pagination{}, err
	}
	return env.Data.Items, env.Data.Pagination, nil
}

// writeForm composes a multipart form for non-JSON uploads (unused by
// disk backend today but reserved for future endpoints).
func writeForm(fields map[string]string) (string, []byte, error) {
	buf := &bytes.Buffer{}
	w := multipart.NewWriter(buf)
	for k, v := range fields {
		if err := w.WriteField(k, v); err != nil {
			return "", nil, err
		}
	}
	if err := w.Close(); err != nil {
		return "", nil, err
	}
	return w.FormDataContentType(), buf.Bytes(), nil
}

// strPtr returns a pointer to s (convenience for optional JSON fields).
func strPtr(s string) *string { return &s }

// atoiOr returns def if s is empty/unparseable.
func atoiOr(s string, def int) int {
	if s == "" {
		return def
	}
	v, err := strconv.Atoi(s)
	if err != nil {
		return def
	}
	return v
}

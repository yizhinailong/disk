package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
)

func newTestClient(server *httptest.Server) *Client {
	return New(WithBaseURL(server.URL))
}

func writeEnvelope(t *testing.T, w http.ResponseWriter, status int, data any) {
	t.Helper()
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		t.Fatalf("encode response: %v", err)
	}
}

func TestDownloadFileRefreshesAndRetriesAfterUnauthorized(t *testing.T) {
	var downloadCalls int
	var refreshCalls int

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/file/download/42":
			downloadCalls++
			if downloadCalls == 1 {
				if got := r.Header.Get("Authorization"); got != "Bearer old-access" {
					t.Fatalf("first download auth = %q", got)
				}
				writeEnvelope(t, w, http.StatusUnauthorized, Envelope[any]{Code: 40108, Message: "expired"})
				return
			}
			if got := r.Header.Get("Authorization"); got != "Bearer new-access" {
				t.Fatalf("retry download auth = %q", got)
			}
			_, _ = io.WriteString(w, "downloaded")
		case "/api/auth/refresh":
			refreshCalls++
			var body map[string]string
			if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
				t.Fatalf("decode refresh request: %v", err)
			}
			if body["refresh_token"] != "old-refresh" {
				t.Fatalf("refresh token = %q", body["refresh_token"])
			}
			writeEnvelope(t, w, http.StatusOK, Envelope[RefreshResponse]{
				Code: CodeSuccess,
				Data: RefreshResponse{AccessToken: "new-access", RefreshToken: "new-refresh"},
			})
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()

	client := newTestClient(server)
	client.SetTokens("old-access", "old-refresh")

	var dst bytes.Buffer
	_, err := client.DownloadFile(context.Background(), 42, &dst)
	if err != nil {
		t.Fatalf("DownloadFile returned error: %v", err)
	}
	if dst.String() != "downloaded" {
		t.Fatalf("download body = %q", dst.String())
	}
	if downloadCalls != 2 || refreshCalls != 1 {
		t.Fatalf("downloadCalls=%d refreshCalls=%d", downloadCalls, refreshCalls)
	}
	if client.AccessToken() != "new-access" || client.RefreshToken() != "new-refresh" {
		t.Fatalf("tokens were not updated after refresh")
	}
}

func TestDownloadFileRefreshFailureReturnsError(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/file/download/42":
			writeEnvelope(t, w, http.StatusUnauthorized, Envelope[any]{Code: 40108, Message: "expired"})
		case "/api/auth/refresh":
			writeEnvelope(t, w, http.StatusUnauthorized, Envelope[any]{Code: 40108, Message: "refresh expired"})
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()

	client := newTestClient(server)
	client.SetTokens("old-access", "old-refresh")

	var dst bytes.Buffer
	_, err := client.DownloadFile(context.Background(), 42, &dst)
	if err == nil {
		t.Fatal("expected refresh failure error")
	}
	var apiErr *APIError
	if !errors.As(err, &apiErr) {
		t.Fatalf("expected APIError, got %T: %v", err, err)
	}
	if apiErr.URL != "/api/auth/refresh" {
		t.Fatalf("error URL = %q", apiErr.URL)
	}
}

func TestDownloadFileRangeRefreshesAndPreservesRangeHeader(t *testing.T) {
	var downloadCalls int

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/file/download/42":
			downloadCalls++
			if got := r.Header.Get("Range"); got != "bytes=5-" {
				t.Fatalf("range header = %q", got)
			}
			if downloadCalls == 1 {
				writeEnvelope(t, w, http.StatusUnauthorized, Envelope[any]{Code: 40108, Message: "expired"})
				return
			}
			w.Header().Set("Content-Range", "bytes 5-9/10")
			w.WriteHeader(http.StatusPartialContent)
			_, _ = io.WriteString(w, "67890")
		case "/api/auth/refresh":
			writeEnvelope(t, w, http.StatusOK, Envelope[RefreshResponse]{
				Code: CodeSuccess,
				Data: RefreshResponse{AccessToken: "new-access", RefreshToken: "new-refresh"},
			})
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()

	client := newTestClient(server)
	client.SetTokens("old-access", "old-refresh")

	resp, err := client.DownloadFileRange(context.Background(), 42, 5, -1)
	if err != nil {
		t.Fatalf("DownloadFileRange returned error: %v", err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if string(body) != "67890" {
		t.Fatalf("range body = %q", string(body))
	}
	if downloadCalls != 2 {
		t.Fatalf("downloadCalls = %d", downloadCalls)
	}
}

func TestDownloadShareFileUsesShareTokenWithoutOwnerRefresh(t *testing.T) {
	var refreshCalls int
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/share/download/share-1/42":
			if got := r.Header.Get("X-Share-Token"); got != "share-token" {
				t.Fatalf("share token = %q", got)
			}
			if got := r.Header.Get("Authorization"); got != "" {
				t.Fatalf("visitor share download must not send owner auth header, got %q", got)
			}
			writeEnvelope(t, w, http.StatusUnauthorized, Envelope[any]{Code: 40108, Message: "share expired"})
		case "/api/auth/refresh":
			refreshCalls++
			writeEnvelope(t, w, http.StatusOK, Envelope[RefreshResponse]{Code: CodeSuccess})
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()

	client := newTestClient(server)
	client.SetTokens("owner-access", "owner-refresh")
	client.SetShareToken("share-token")

	var dst bytes.Buffer
	_, err := client.DownloadShareFile(context.Background(), "share-1", 42, &dst)
	if err == nil {
		t.Fatal("expected share download error")
	}
	if refreshCalls != 0 {
		t.Fatalf("owner refresh should not be called for share download, got %d", refreshCalls)
	}
}

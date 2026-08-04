package client

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestAdminShareOperationsUseExternalShareID(t *testing.T) {
	const shareID = "Ab/Cd 12"
	const requestURI = "/api/admin/shares/Ab%2FCd%2012"
	var detailCalls int
	var cancelCalls int

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/admin/shares/"+shareID {
			t.Fatalf("request path = %q", r.URL.Path)
		}
		if r.RequestURI != requestURI {
			t.Fatalf("request URI = %q", r.RequestURI)
		}

		switch r.Method {
		case http.MethodGet:
			detailCalls++
			writeEnvelope(t, w, http.StatusOK, Envelope[AdminShareDetail]{
				Code: CodeSuccess,
				Data: AdminShareDetail{ShareID: shareID},
			})
		case http.MethodDelete:
			cancelCalls++
			writeEnvelope(t, w, http.StatusOK, Envelope[any]{Code: CodeSuccess})
		default:
			t.Fatalf("request method = %q", r.Method)
		}
	}))
	defer server.Close()

	client := newTestClient(server)
	detail, err := client.AdminGetShare(context.Background(), shareID)
	if err != nil {
		t.Fatalf("AdminGetShare returned error: %v", err)
	}
	if detail.ShareID != shareID {
		t.Fatalf("share_id = %q", detail.ShareID)
	}
	if err := client.AdminForceCancelShare(context.Background(), shareID); err != nil {
		t.Fatalf("AdminForceCancelShare returned error: %v", err)
	}
	if detailCalls != 1 || cancelCalls != 1 {
		t.Fatalf("detailCalls=%d cancelCalls=%d", detailCalls, cancelCalls)
	}
}

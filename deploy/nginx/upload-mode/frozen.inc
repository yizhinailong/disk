location = /api/file/upload {
    default_type application/json;
    add_header Retry-After 30 always;
    add_header Cache-Control "no-store" always;
    return 503 '{"code":50013,"message":"Upload lifecycle is temporarily frozen for rollback","data":null}';
}

location ^~ /api/file/upload/ {
    default_type application/json;
    add_header Retry-After 30 always;
    add_header Cache-Control "no-store" always;
    return 503 '{"code":50013,"message":"Upload lifecycle is temporarily frozen for rollback","data":null}';
}

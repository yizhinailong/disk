upstream disk_api {
    zone disk_api 64k;
    least_conn;
    include /etc/nginx/conf.d/disk-api-upstreams.inc;
    keepalive 32;
}

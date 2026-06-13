package client

import (
	"crypto/md5"
	"encoding/hex"
	"io"
	"os"
)

// md5Hex returns the lowercase hex MD5 of data.
func md5Hex(data []byte) string {
	sum := md5.Sum(data)
	return hex.EncodeToString(sum[:])
}

// fileMD5 returns the lowercase hex MD5 of the file at path.
func fileMD5(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := md5.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

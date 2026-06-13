# disk-tui — makefile

GO ?= go
BIN ?= bin/disk-tui

.PHONY: all build run vet fmt tidy clean

all: tidy build

build:
	$(GO) build -o $(BIN) .

run:
	$(GO) run .

vet:
	$(GO) vet ./...

fmt:
	$(GO) fmt ./...

tidy:
	$(GO) mod tidy

clean:
	rm -rf bin

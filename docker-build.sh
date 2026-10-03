#!/usr/bin/env bash
set -e

echo "=== AthenaEnv Docker Build ==="
docker compose run --rm build

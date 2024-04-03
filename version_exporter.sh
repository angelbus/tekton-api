#!/bin/bash
set -euo pipefail
export IMAGE_VERSION=$(cat "VERSION.txt")
echo "${IMAGE_VERSION}"
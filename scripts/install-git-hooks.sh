#!/usr/bin/env sh
set -eu

git config --local core.hooksPath .githooks
printf 'Git hooks installed: core.hooksPath=.githooks\n'

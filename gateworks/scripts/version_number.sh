#!/bin/sh

DIRTY="$(git diff --shortstat)"
printf "%s@%s\n" \
    "$(git rev-parse --abbrev-ref HEAD)" \
    "$(git rev-parse --short HEAD)${DIRTY:+-dirty}"
